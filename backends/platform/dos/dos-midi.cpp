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

/* MIDI out through an MPU-401 in UART mode (a Sound Blaster's MIDI port, an
 * MPU-401 or compatible), to an MT-32 or a General MIDI module. It is offered as
 * two devices, "Roland MT-32" and "General MIDI", which differ only in what
 * ScummVM sends them.
 *
 * Port (hex): SCUMMVM_DOS_MPU, else mpu_port, else BLASTER's P field, else 330.
 * SCUMMVM_DOS_MIDILOG=1 logs the first messages sent.
 */

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/scummsys.h"

#if defined(USE_DOS_DRIVER)

#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio/mpu401.h"
#include "audio/musicplugin.h"
#include "common/config-manager.h"
#include "common/error.h"
#include "common/str.h"
#include "common/textconsole.h"

// In dos.cpp: keeps the sound card fed while a long message goes out at MIDI speed
void dosPumpAudio();

namespace {

// Status port bits
const unsigned kDataNotReady = 0x80; ///< DSR: clear when a byte can be read
const unsigned kNotReadyToWrite = 0x40; ///< DRR: clear when a byte can be written

const byte kAck = 0xFE;

unsigned g_port = 0;      ///< Data port; the command/status port is the next one
int g_probed = -1;        ///< -1: not tried yet, 0: nothing there, 1: found

uint64 nowMicros() {
	return (uint64)uclock() * 1000000ULL / UCLOCKS_PER_SEC;
}

inline unsigned statusPort() {
	return g_port + 1;
}

unsigned parseHex(const char *text) {
	unsigned value = 0;
	bool any = false;
	for (; *text; ++text) {
		int digit;
		if (*text >= '0' && *text <= '9')
			digit = *text - '0';
		else if (*text >= 'a' && *text <= 'f')
			digit = *text - 'a' + 10;
		else if (*text >= 'A' && *text <= 'F')
			digit = *text - 'A' + 10;
		else
			break;
		value = value * 16 + digit;
		any = true;
	}
	return any ? value : 0;
}

unsigned chooseBase() {
	if (const char *env = getenv("SCUMMVM_DOS_MPU")) {
		unsigned port = parseHex(env);
		if (port)
			return port;
	}
	if (ConfMan.hasKey("mpu_port")) {
		unsigned port = parseHex(ConfMan.get("mpu_port").c_str());
		if (port)
			return port;
	}
	if (const char *blaster = getenv("BLASTER")) {
		for (const char *p = blaster; *p; ++p) {
			// A field is a letter followed by its value; fields are separated by spaces.
			if ((*p == 'P' || *p == 'p') && (p == blaster || p[-1] == ' ')) {
				unsigned port = parseHex(p + 1);
				if (port)
					return port;
			}
		}
	}
	return 0x330;
}

/** Wait up to `micros` for a byte to be readable; returns it, or -1. */
int readByte(unsigned micros) {
	uint64 start = nowMicros();
	do {
		if (!(inportb(statusPort()) & kDataNotReady))
			return inportb(g_port);
	} while (nowMicros() - start < micros);
	return -1;
}

/** Wait up to `micros` for the interface to accept a byte. */
bool waitWritable(unsigned micros) {
	uint64 start = nowMicros();
	do {
		if (!(inportb(statusPort()) & kNotReadyToWrite))
			return true;
	} while (nowMicros() - start < micros);
	return false;
}

void drainInput() {
	for (int i = 0; i < 64 && !(inportb(statusPort()) & kDataNotReady); i++)
		(void)inportb(g_port);
}

/** Send a command and wait for the interface to acknowledge it. */
bool command(byte cmd) {
	if (!waitWritable(20000))
		return false;
	drainInput();
	outportb(statusPort(), cmd);
	// Some interfaces answer only after a little while
	for (int i = 0; i < 4; i++) {
		int reply = readByte(25000);
		if (reply == kAck)
			return true;
		if (reply < 0)
			break;
	}
	return false;
}

/** Look for an MPU-401 at the chosen port and put it in UART mode. */
bool probe() {
	if (g_probed >= 0)
		return g_probed != 0;

	g_probed = 0;
	g_port = chooseBase();

	// Reset (twice if need be: an interface mid-message can miss one), then UART mode
	bool reset = command(0xFF) || command(0xFF);
	if (reset && command(0x3F))
		g_probed = 1;

	return g_probed != 0;
}

} // End of anonymous namespace

class MidiDriver_DOS : public MidiDriver_MPU401 {
public:
	explicit MidiDriver_DOS(bool mt32) : _open(false), _mt32(mt32), _bytes(0), _noteOns(0),
		_sysexCount(0), _timeouts(0), _logged(0) {
		_log = getenv("SCUMMVM_DOS_MIDILOG") != nullptr;
	}

	int open() override {
		if (_open)
			return MERR_ALREADY_OPEN;
		if (!probe())
			return MERR_DEVICE_NOT_AVAILABLE;

		_open = true;
		warning("DOS MIDI: MPU-401 at %x in UART mode, %s", g_port, _mt32 ? "Roland MT-32" : "General MIDI");
		return 0;
	}

	bool isOpen() const override {
		return _open;
	}

	void close() override {
		if (!_open)
			return;
		MidiDriver_MPU401::close(); // all notes off
		_open = false;
		warning("DOS MIDI: %u bytes sent, %u note-ons, %u sysex, %u timeouts", (unsigned)_bytes, (unsigned)_noteOns, (unsigned)_sysexCount, (unsigned)_timeouts);
	}

	void send(uint32 b) override {
		if (!_open)
			return;

		midiDriverCommonSend(b);

		byte status = b & 0xFF;
		byte data1 = (b >> 8) & 0xFF;
		byte data2 = (b >> 16) & 0xFF;

		if (status >= 0x80 && status < 0xF0) {
			put(status);
			put(data1);
			if ((status & 0xF0) != 0xC0 && (status & 0xF0) != 0xD0)
				put(data2);
			if ((status & 0xF0) == 0x90 && data2)
				_noteOns++;
			logMessage(status, data1, data2);
		} else if (status >= 0xF8) {
			put(status); // real-time: a single byte
		}
	}

	void sysEx(const byte *msg, uint16 length) override {
		if (!_open)
			return;

		midiDriverCommonSysEx(msg, length);

		put(0xF0);
		for (uint16 i = 0; i < length; i++)
			put(msg[i]);
		put(0xF7);

		_sysexCount++;
		if (_log && _logged < 40) {
			_logged++;
			Common::String hex;
			for (uint16 i = 0; i < length && i < 12; i++)
				hex += Common::String::format("%02X ", msg[i]);
			warning("DOS MIDI: sysex %u bytes: %s%s", length, hex.c_str(), length > 12 ? "..." : "");
		}
	}

private:
	/** Write one byte, waiting for room at MIDI speed. */
	void put(byte value) {
		// 31250 baud: a byte takes about 320 microseconds, and a typical interface
		// holds only a few. Waiting in short steps lets the sound card stay fed.
		uint64 start = nowMicros();
		unsigned spins = 0;
		while (inportb(statusPort()) & kNotReadyToWrite) {
			if (nowMicros() - start > 200000) {
				_timeouts++; // nothing is draining the interface: drop the byte
				return;
			}
			if (++spins % 64 == 0)
				dosPumpAudio();
		}
		outportb(g_port, value);
		_bytes++;
		if (_log && (_bytes % 512) == 0)
			warning("DOS MIDI: progress %u bytes, %u note-ons, %u sysex, %u timeouts",
			        (unsigned)_bytes, (unsigned)_noteOns, (unsigned)_sysexCount, (unsigned)_timeouts);

		// Replies and active sensing from a smart interface are never needed
		if (!(inportb(statusPort()) & kDataNotReady))
			(void)inportb(g_port);
	}

	void logMessage(byte status, byte data1, byte data2) {
		if (_log && _logged < 40) {
			_logged++;
			warning("DOS MIDI: %02X %02X %02X", status, data1, data2);
		}
	}

	bool _open;
	bool _mt32;
	uint32 _bytes;
	uint32 _noteOns;
	uint32 _sysexCount;
	uint32 _timeouts;
	uint32 _logged;
	bool _log;
};

// Plugin interface

class DOSMidiMusicPlugin : public MusicPluginObject {
public:
	const char *getName() const override {
		return "DOS MPU-401 MIDI";
	}

	const char *getId() const override {
		return "dosmidi";
	}

	MusicDevices getDevices() const override {
		MusicDevices devices;
		// Listed only if an interface answers, so a PC without one is never offered it
		if (probe()) {
			devices.push_back(MusicDevice(this, "Roland MT-32", MT_MT32));
			devices.push_back(MusicDevice(this, "General MIDI", MT_GM));
		}
		return devices;
	}

	Common::Error createInstance(MidiDriver **mididriver, MidiDriver::DeviceHandle dev = 0) const override {
		bool mt32 = dev && MidiDriver::getMusicType(dev) == MT_MT32;
		*mididriver = new MidiDriver_DOS(mt32);
		return Common::kNoError;
	}

	bool checkDevice(MidiDriver::DeviceHandle hdl, int checkFlags, bool quiet) const override {
		// Never chosen by "<default>": an interface with nothing connected would be silent
		return (checkFlags & MDCK_AUTO) == 0 && probe();
	}
};

REGISTER_PLUGIN_STATIC(DOSMIDI, PLUGIN_TYPE_MUSIC, DOSMidiMusicPlugin);

#endif // USE_DOS_DRIVER
