/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/* OPL for the FM chip of an AdLib or Sound Blaster card, through its I/O ports
 * (0x388/0x389; 0x38A/0x38B for an OPL3's second register set). The chip plays
 * by itself, so the CPU only writes registers instead of emulating it.
 */

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/scummsys.h"

#if defined(USE_DOS_DRIVER)

#include <pc.h>
#include <stdlib.h>
#include <time.h>

#include "audio/fmopl.h"
#include "common/textconsole.h"

namespace OPL {
namespace DOSHardware {

// AdLib compatible ports: address (and status when read) and data
static const unsigned kAddressPort = 0x388;
static const unsigned kDataPort = 0x389;

// Second register set of an OPL3 (Sound Blaster Pro 2 / 16 and later)
static const unsigned kAddressPort2 = 0x38A;
static const unsigned kDataPort2 = 0x38B;

static unsigned g_writeCount = 0;

// True if the chip is an OPL3 (YMF262): found out while testing for the chip
static bool g_isOpl3 = false;

// True while a driver owns the chip, which would keep sounding after the program ends
static bool g_chipInUse = false;
static bool g_exitHandlerSet = false;
static int g_presentCached = -1;

/** Number of register writes since the last call, for the OPL3 self-test. */
unsigned takeWriteCount() {
	unsigned n = g_writeCount;
	g_writeCount = 0;
	return n;
}

// The chip needs 3.3 us after a register number and 23 us after a value; each
// status port read takes about 1 us on the ISA bus
static inline void settle(int reads) {
	for (int i = 0; i < reads; i++)
		(void)inportb(kAddressPort);
}

// reg 0x100..0x1FF is the second register set, which only an OPL3 has
static void writeRegister(unsigned reg, unsigned value) {
	if (reg >= 0x100) {
		outportb(kAddressPort2, reg & 0xFF);
		settle(6);
		outportb(kDataPort2, value);
	} else {
		outportb(kAddressPort, reg);
		settle(6);
		outportb(kDataPort, value);
	}
	settle(35);
	g_writeCount++;
}

// By the timer chip, not port reads, so the wait is right under emulators too
static void waitMicroseconds(unsigned us) {
	uclock_t start = uclock();
	uclock_t ticks = (uclock_t)us * UCLOCKS_PER_SEC / 1000000 + 1;
	while (uclock() - start < ticks) {
	}
}

/** Key off every voice. Used when the program exits however it exits. */
static void silenceChip() {
	for (unsigned r = 0xB0; r <= 0xB8; r++)
		writeRegister(r, 0x00); // bit 5 clear: key off
	writeRegister(0xBD, 0x00);  // rhythm mode and percussion off

	if (g_isOpl3) {
		for (unsigned r = 0x1B0; r <= 0x1B8; r++)
			writeRegister(r, 0x00);
		writeRegister(0x105, 0x00); // back to OPL2 mode for whoever comes next
	}
}

static void silenceAtExit() {
	if (g_chipInUse) {
		silenceChip();
		g_chipInUse = false;
	}
}

/** The standard test for an OPL2 or later: only a started timer sets its overflow flag. */
bool isPresent() {
	if (g_presentCached >= 0)
		return g_presentCached != 0;

	writeRegister(0x04, 0x60);                 // reset both timers
	writeRegister(0x04, 0x80);                 // reset the interrupt flag
	unsigned statusBefore = inportb(kAddressPort);
	unsigned before = statusBefore & 0xE0;

	writeRegister(0x02, 0xFF);                 // timer 1: overflow almost at once
	writeRegister(0x04, 0x21);                 // start timer 1
	waitMicroseconds(150);                     // it overflows after 80
	unsigned after = inportb(kAddressPort) & 0xE0;

	writeRegister(0x04, 0x60);                 // reset the timers again
	writeRegister(0x04, 0x80);

	g_presentCached = (before == 0x00 && after == 0xC0) ? 1 : 0;

	// An OPL2 leaves bits 1 and 2 of the status register set, an OPL3 clears them
	g_isOpl3 = g_presentCached && (statusBefore & 0x06) == 0x00;
	g_writeCount = 0;
	return g_presentCached != 0;
}

/** True if the chip found by isPresent() is an OPL3. */
bool isOpl3() {
	return isPresent() && g_isOpl3;
}

class OPL : public ::OPL::OPL, public Audio::RealChip {
public:
	explicit OPL(Config::OplType type) : _type(type), _index(0), _ready(false) {}

	~OPL() override {
		stop();
		if (_ready) {
			reset(); // leave the chip silent...
			silenceChip(); // ...and back in OPL2 mode, whatever reset() set up
			g_chipInUse = false;
		}
	}

	bool init() override {
		if (!isPresent())
			return false;

		// Two chips, or an OPL3, need the second register set: only an OPL3 has it
		if (_type != Config::kOpl2 && !g_isOpl3)
			return false;

		reset();
		_ready = true;

		if (!g_exitHandlerSet) {
			atexit(silenceAtExit);
			g_exitHandlerSet = true;
		}
		g_chipInUse = true;
		return true;
	}

	void reset() override {
		// OPL3 mode off, so games get the OPL2 they expect (an OPL2 ignores this port)
		writeRegister(0x105, 0x00);

		const unsigned sets = g_isOpl3 ? 2 : 1;
		for (unsigned set = 0; set < sets; set++) {
			unsigned base = set * 0x100;
			for (unsigned r = 0x01; r <= 0xF5; r++) {
				if (r == 0x05 && set == 1)
					continue; // the OPL3 enable, left off
				writeRegister(base + r, 0);
			}

			// 0x40..0x55: each operator's attenuation, 0x3F is silent
			for (unsigned r = 0x40; r <= 0x55; r++)
				writeRegister(base + r, 0x3F);
		}

		_index = 0;
		_rhythmMode = false;
		for (int i = 0; i < 3; i++)
			_connectionFeedbackValues[i] = 0;

		// Dual OPL2 on an OPL3: the two chips are the OPL3's left and right halves
		initDualOpl2OnOpl3(_type);
	}

	void write(int a, int v) override {
		if (a & 1) {
			writeReg(_index, v);
		} else if (_type == Config::kOpl2) {
			_index = v & 0xFF;
		} else {
			// Bit 1 of the port picks the register set (0x222/0x223 and 0x38A/0x38B)
			_index = ((a & 2) << 7) | (v & 0xFF);
		}
	}

	void writeReg(int r, int v) override {
		if (_type == Config::kOpl2) {
			if (r < 0 || r > 0xFF)
				return; // only the first register set (OPL2) is wanted
		} else if (r < 0 || r > 0x1FF) {
			return;
		}

		if (!emulateDualOpl2OnOpl3(r, v, _type))
			return; // handled there, with the writes it replaces them with

		writeRegister((unsigned)r, (unsigned)(v & 0xFF));
	}

private:
	Config::OplType _type;
	int _index;
	bool _ready;
};

::OPL::OPL *create(Config::OplType type) {
	return new OPL(type);
}

} // End of namespace DOSHardware
} // End of namespace OPL

// For OSystem_DOS::quit(), which leaves through _exit() and so runs no atexit handlers
void dosOplSilence() {
	OPL::DOSHardware::silenceAtExit();
}

/**
 * Testing aid (SCUMMVM_DOS_OPL3TEST=1): ask for an OPL3 as a game would, and
 * write to both register sets. The result goes to the log.
 */
void dosOpl3SelfTest() {
	OPL::Config::DriverId drv = OPL::Config::detect(OPL::Config::kOpl3);
	if (drv < 0) {
		warning("DOSOPL3TEST: OPL3 request declined (no OPL3 chip)");
		return;
	}

	OPL::OPL *opl = OPL::Config::create(drv, OPL::Config::kOpl3);
	if (!opl || !opl->init()) {
		warning("DOSOPL3TEST: could not start driver %d", (int)drv);
		delete opl;
		return;
	}

	OPL::DOSHardware::takeWriteCount();
	opl->writeReg(0x105, 0x01);  // OPL3 mode on
	opl->writeReg(0x1C0, 0x30);  // second set: voice 0 to both speakers
	opl->writeReg(0x0C0, 0x30);  // first set
	opl->write(0x38A, 0xC1);     // port level: second set, register 0xC1...
	opl->write(0x38B, 0x30);     // ...written through the data port
	unsigned writes = OPL::DOSHardware::takeWriteCount();
	delete opl;

	warning("DOSOPL3TEST: OK driver %d (%s), %u register writes, OPL3 chip",
	        (int)drv, drv == 20 ? "hardware" : "emulator", writes);
}

#endif // USE_DOS_DRIVER
