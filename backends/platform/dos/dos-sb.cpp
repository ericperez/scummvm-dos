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

// This file talks directly to the Sound Blaster, the DMA controller and the PIC
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <ctype.h>
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>
#include <time.h>

#include "backends/platform/dos/dos-sb.h"

static SoundBlaster *g_active = nullptr;

// Stop the card even if the program exits through exit()/error(): left
// running, the DMA controller would keep reading from freed memory.
static void shutdownAtExit() {
	if (g_active)
		g_active->shutdown();
}

// For OSystem_DOS::quit(), which leaves through _exit() and so runs no atexit handlers
void dosSoundBlasterShutdown() {
	shutdownAtExit();
}

// The interrupt side: runs inside the card's IRQ, so only locked memory and no
// library calls. Each block the card has played is filled with silence, so a
// busy program leaves silence rather than repeating old sound.

struct IsrState {
	uint16_t ack8;               ///< DSP status port that acknowledges an 8-bit interrupt
	uint16_t ack16;              ///< and a 16-bit one (0 when not in 16-bit mode)
	uint16_t dmaCount;           ///< current-count port of the DMA channel in use
	uint16_t dmaClear;           ///< flip-flop reset port of that channel's controller
	uint16_t dosSel;             ///< selector for all of conventional memory
	uint8_t wide;                ///< 1: the channel counts in words (16-bit DMA)
	uint8_t slave;               ///< 1: the IRQ is on the slave PIC
	uint8_t blockShift;          ///< log2 of the block size in bytes
	uint8_t checkSpurious;       ///< IRQ 7 / 15: a spurious interrupt looks the same, check the PIC
	uint16_t picCmd;             ///< command port of the PIC the IRQ is on (0x20 or 0xA0)
	uint32_t ringPhys;           ///< where the ring is
	uint32_t ringBytes;
	uint32_t fill;               ///< silence, as 32 bits
	volatile uint32_t irqCount;  ///< interrupts seen so far
};

static IsrState g_isr;
static _go32_dpmi_seginfo g_oldIrq;
static _go32_dpmi_seginfo g_newIrq;
static int g_irqVector = 0;
static bool g_exitHookSet = false;

static inline __attribute__((always_inline)) unsigned char isrIn(unsigned short port) {
	unsigned char value;
	asm volatile("inb %1, %0" : "=a"(value) : "d"(port));
	return value;
}

static inline __attribute__((always_inline)) void isrOut(unsigned short port, unsigned char value) {
	asm volatile("outb %0, %1" : : "a"(value), "d"(port));
}

static void __attribute__((noinline, used)) sbIrqHandler() {
	// IRQ 7 and 15 can be spurious: if the in-service register does not show ours, leave it
	if (g_isr.checkSpurious) {
		isrOut(g_isr.picCmd, 0x0B);
		if (!(isrIn(g_isr.picCmd) & 0x80)) {
			if (g_isr.slave)
				isrOut(0x20, 0x20);
			return;
		}
	}

	// Acknowledge the card, or it would never raise another interrupt
	isrIn(g_isr.ack8);
	if (g_isr.ack16)
		isrIn(g_isr.ack16);

	// The DMA count is read a byte at a time: read until two reads agree
	unsigned count = 0, previous = 0xFFFFFFFF;
	for (int i = 0; i < 6; i++) {
		isrOut(g_isr.dmaClear, 0);
		unsigned lo = isrIn(g_isr.dmaCount);
		unsigned hi = isrIn(g_isr.dmaCount);
		count = lo | (hi << 8);
		if (count == previous)
			break;
		previous = count;
	}

	uint32_t remaining = (count + 1) << g_isr.wide;
	uint32_t pos = remaining > g_isr.ringBytes ? 0 : g_isr.ringBytes - remaining;

	// The block that has just finished is the one before the block now playing
	uint32_t finished = ((pos >> g_isr.blockShift) + 3) & 3;
	uint32_t address = g_isr.ringPhys + (finished << g_isr.blockShift);
	uint32_t dwords = (uint32_t)1 << (g_isr.blockShift - 2);

	asm volatile(
	    "pushw %%es\n\t"
	    "movw %w3, %%es\n\t"
	    "cld\n\t"
	    "rep stosl\n\t"
	    "popw %%es"
	    : "+D"(address), "+c"(dwords)
	    : "a"(g_isr.fill), "r"((unsigned)g_isr.dosSel)
	    : "memory", "cc");

	g_isr.irqCount++;

	// End of interrupt (the slave first, then the master it is cascaded into)
	if (g_isr.slave)
		isrOut(0xA0, 0x20);
	isrOut(0x20, 0x20);
}

// Marks the end of sbIrqHandler() so its size can be locked
static void __attribute__((noinline, used)) sbIrqHandlerEnd() {
	asm volatile("nop");
}

enum {
	k8BitRingBytes = SoundBlaster::kRingFrames,         ///< 8-bit mono: one byte a frame
	k16BitRingBytes = SoundBlaster::kRingFrames * 4,    ///< 16-bit stereo: four bytes a frame
	kBlockBytes = k16BitRingBytes * 2                   ///< room to align the biggest ring to a DMA page
};

SoundBlaster::SoundBlaster() :
	_active(false), _bits16(false),
	_base(0), _irq(-1), _dma(-1), _hdma(-1),
	_dspMajor(0), _dspMinor(0), _rate(0), _ringBytes(0),
	_dosSelector(-1), _blockPhys(0), _physAddr(0),
	_irqMasked(false), _origPicMask(0), _irqPort(0), _irqBit(0), _cascadeTouched(false), _origMasterMask(0),
	_irqInstalled(false),
	_activeChannel(-1) {
	_error[0] = 0;
}

SoundBlaster::~SoundBlaster() {
	shutdown();
}

void SoundBlaster::setError(const char *text) {
	size_t used = strlen(_error);
	if (used + 3 >= sizeof(_error))
		return;
	snprintf(_error + used, sizeof(_error) - used, "%s%s", used ? "; " : "", text);
}

bool SoundBlaster::dspWrite(uint8_t value) {
	for (unsigned i = 0; i < 0x10000; i++) {
		if (!(inportb(_base + 0xC) & 0x80)) {
			outportb(_base + 0xC, value);
			return true;
		}
	}
	return false;
}

int SoundBlaster::dspRead() {
	for (unsigned i = 0; i < 0x10000; i++) {
		if (inportb(_base + 0xE) & 0x80)
			return inportb(_base + 0xA);
	}
	return -1;
}

bool SoundBlaster::resetDsp(unsigned base) {
	outportb(base + 0x6, 1);
	delay(1);
	outportb(base + 0x6, 0);

	// The DSP answers 0xAA within about 100 microseconds; allow 100 ms
	for (int i = 0; i < 100; i++) {
		if ((inportb(base + 0xE) & 0x80) && inportb(base + 0xA) == 0xAA)
			return true;
		delay(1);
	}
	return false;
}

bool SoundBlaster::probe(unsigned base) {
	if (!resetDsp(base))
		return false;

	_base = base;
	if (!dspWrite(0xE1)) // get DSP version
		return false;

	_dspMajor = dspRead();
	_dspMinor = dspRead();
	return _dspMajor >= 0 && _dspMinor >= 0;
}

bool SoundBlaster::allocateBlock() {
	int selector = -1;
	int segment = __dpmi_allocate_dos_memory((kBlockBytes + 15) / 16, &selector);
	if (segment == -1)
		return false;

	_dosSelector = selector;
	_blockPhys = (uint32_t)segment << 4;
	return true;
}

// Choose where in the block the ring goes: DMA cannot cross a page boundary
// (64KB for the 8-bit channels, 128KB for the 16-bit ones).
bool SoundBlaster::placeRing(unsigned ringBytes, unsigned boundary) {
	uint32_t start = _blockPhys;
	uint32_t last = start + ringBytes - 1;
	if ((start & ~(boundary - 1)) != (last & ~(boundary - 1)))
		start = (start + boundary - 1) & ~(boundary - 1);

	if (start + ringBytes > _blockPhys + kBlockBytes)
		return false;

	_physAddr = start;
	_ringBytes = ringBytes;
	return true;
}

void SoundBlaster::fillSilence() {
	// unsigned 8-bit silence is 0x80; signed 16-bit silence is 0
	uint8_t silence[256];
	memset(silence, _bits16 ? 0x00 : 0x80, sizeof(silence));
	for (unsigned off = 0; off < _ringBytes; off += sizeof(silence))
		dosmemput(silence, sizeof(silence), _physAddr + off);
}

SoundBlaster::DmaPorts SoundBlaster::portsFor(int channel) const {
	static const uint16_t page8[4] = { 0x87, 0x83, 0x81, 0x82 };
	static const uint16_t page16[4] = { 0x8F, 0x8B, 0x89, 0x8A };

	DmaPorts p;
	if (channel < 4) {
		p.addr = (uint16_t)(channel * 2);
		p.count = p.addr + 1;
		p.page = page8[channel];
		p.mask = 0x0A;
		p.mode = 0x0B;
		p.clear = 0x0C;
	} else {
		int sub = channel - 4;
		p.addr = (uint16_t)(0xC0 + sub * 4);
		p.count = p.addr + 2;
		p.page = page16[sub];
		p.mask = 0xD4;
		p.mode = 0xD6;
		p.clear = 0xD8;
	}
	return p;
}

void SoundBlaster::programDma(int channel, unsigned ringBytes) {
	const DmaPorts p = portsFor(channel);
	const int sub = channel & 3;
	const bool wide = channel >= 4;

	// The 16-bit channels count in words, and their address is a word address
	const uint32_t addr = wide ? (_physAddr >> 1) : _physAddr;
	const unsigned count = (wide ? ringBytes / 2 : ringBytes) - 1;

	outportb(p.mask, 4 | sub);          // mask the channel while we change it
	outportb(p.clear, 0);               // clear the byte flip-flop
	outportb(p.mode, 0x58 | sub);       // single mode, auto-init, read (memory -> card)
	outportb(p.addr, addr & 0xFF);
	outportb(p.addr, (addr >> 8) & 0xFF);
	outportb(p.page, (_physAddr >> 16) & 0xFF);
	outportb(p.clear, 0);
	outportb(p.count, count & 0xFF);
	outportb(p.count, (count >> 8) & 0xFF);
	outportb(p.mask, sub);              // unmask: the controller is ready
}

// 8-bit unsigned mono, DSP 2.00 or later (the original auto-init commands).
bool SoundBlaster::start8(int channel, unsigned requestedRate) {
	// The DSP rate is 1000000 / (256 - timeConstant), at most 22222 Hz on an SB 2.0
	if (requestedRate < 4000)
		requestedRate = 4000;
	unsigned divisor = (1000000 + requestedRate / 2) / requestedRate;
	if (divisor < 45)
		divisor = 45;
	if (divisor > 250)
		divisor = 250;
	const uint8_t timeConstant = (uint8_t)(256 - divisor);

	_bits16 = false;
	if (!placeRing(k8BitRingBytes, 0x10000))
		return false;
	fillSilence();

	if (!resetDsp(_base))
		return false;
	programDma(channel, _ringBytes);
	_activeChannel = channel;
	_dma = channel;
	_rate = 1000000 / divisor;

	const unsigned block = _ringBytes / kBlocks - 1; // the card interrupts after each block
	bool ok = dspWrite(0xD1);                        // speaker on
	ok = ok && dspWrite(0x40);                 // set time constant
	ok = ok && dspWrite(timeConstant);
	ok = ok && dspWrite(0x48);                 // set DMA block size
	ok = ok && dspWrite(block & 0xFF);
	ok = ok && dspWrite((block >> 8) & 0xFF);
	ok = ok && dspWrite(0x1C);                 // start 8-bit auto-init DMA playback
	return ok;
}

// SB16 (DSP 4.xx): 16-bit signed stereo on a 16-bit DMA channel.
bool SoundBlaster::start16(unsigned requestedRate) {
	unsigned rate = requestedRate;
	if (rate < 5000)
		rate = 5000;
	if (rate > 44100)
		rate = 44100;

	_bits16 = true;
	if (!placeRing(k16BitRingBytes, 0x20000))
		return false;
	fillSilence();

	if (!resetDsp(_base))
		return false;
	programDma(_hdma, _ringBytes);
	_activeChannel = _hdma;
	_rate = rate;

	const unsigned block = _ringBytes / kBlocks / 2 - 1; // one block, in 16-bit samples, less one
	bool ok = dspWrite(0x41);                      // set output sample rate
	ok = ok && dspWrite((rate >> 8) & 0xFF);
	ok = ok && dspWrite(rate & 0xFF);
	ok = ok && dspWrite(0xB6);                     // 16-bit output, auto-init, FIFO on
	ok = ok && dspWrite(0x30);                     // signed, stereo
	ok = ok && dspWrite(block & 0xFF);
	ok = ok && dspWrite((block >> 8) & 0xFF);
	return ok;
}

// Is the DMA controller really moving through the ring? Give it 200 ms.
bool SoundBlaster::dmaRunning() {
	const unsigned first = getPlayPosition();
	const uclock_t begin = uclock();
	const uclock_t limit = UCLOCKS_PER_SEC / 5;

	while (uclock() - begin < limit) {
		unsigned moved = (getPlayPosition() + kRingFrames - first) % kRingFrames;
		if (moved >= 16 && moved < kRingFrames / 2)
			return true;
	}
	return false;
}

// The SB16 mixer knows which IRQ the card is set to (register 0x80).
int SoundBlaster::readSb16Irq() {
	outportb(_base + 0x4, 0x80);
	uint8_t value = inportb(_base + 0x5);
	if (value & 0x01)
		return 2;
	if (value & 0x02)
		return 5;
	if (value & 0x04)
		return 7;
	if (value & 0x08)
		return 10;
	return -1;
}

// Install the handler that keeps the ring silent behind the play position,
// unmask the card's IRQ, and check that interrupts do arrive.
bool SoundBlaster::installIrq() {
	if (!_irqMasked) {
		setError("no IRQ known for the card (set BLASTER), so no underrun protection");
		return false;
	}

	const unsigned blockBytes = _ringBytes / kBlocks;
	unsigned shift = 0;
	while ((1u << shift) < blockBytes)
		shift++;

	const DmaPorts p = portsFor(_activeChannel);
	memset(&g_isr, 0, sizeof(g_isr));
	g_isr.ack8 = (uint16_t)(_base + 0xE);
	g_isr.ack16 = _bits16 ? (uint16_t)(_base + 0xF) : 0;
	g_isr.dmaCount = p.count;
	g_isr.dmaClear = p.clear;
	g_isr.dosSel = (uint16_t)_dos_ds;
	g_isr.wide = _activeChannel >= 4 ? 1 : 0;
	g_isr.slave = _irq >= 8 ? 1 : 0;
	g_isr.blockShift = (uint8_t)shift;
	g_isr.ringPhys = _physAddr;
	g_isr.ringBytes = _ringBytes;
	g_isr.fill = _bits16 ? 0x00000000u : 0x80808080u;
	g_isr.checkSpurious = (_irq == 7 || _irq == 15) ? 1 : 0;
	g_isr.picCmd = _irq < 8 ? 0x20 : 0xA0;

	// What the handler touches must never be paged out
	if (_go32_dpmi_lock_code((void *)sbIrqHandler, 2048) != 0 || _go32_dpmi_lock_data(&g_isr, sizeof(g_isr)) != 0) {
		setError("could not lock the interrupt handler in memory");
		return false;
	}

	g_irqVector = _irq < 8 ? 0x08 + _irq : 0x70 + (_irq - 8);
	if (_go32_dpmi_get_protected_mode_interrupt_vector(g_irqVector, &g_oldIrq) != 0) {
		setError("could not read the interrupt vector");
		return false;
	}

	memset(&g_newIrq, 0, sizeof(g_newIrq));
	g_newIrq.pm_offset = (unsigned long)sbIrqHandler;
	g_newIrq.pm_selector = _go32_my_cs();
	if (_go32_dpmi_allocate_iret_wrapper(&g_newIrq) != 0) {
		setError("could not allocate an interrupt wrapper");
		return false;
	}
	if (_go32_dpmi_set_protected_mode_interrupt_vector(g_irqVector, &g_newIrq) != 0) {
		_go32_dpmi_free_iret_wrapper(&g_newIrq);
		setError("could not set the interrupt vector");
		return false;
	}

	// Acknowledge anything the card already has pending (it would hold the
	// line up and no new interrupt could follow), then let the IRQ through
	(void)inportb(_base + 0xE);
	if (_bits16)
		(void)inportb(_base + 0xF);

	outportb(_irqPort, inportb(_irqPort) & ~_irqBit);
	if (_irq >= 8) {
		_origMasterMask = inportb(0x21);
		_cascadeTouched = true;
		outportb(0x21, _origMasterMask & ~0x04);
	}
	_irqInstalled = true;

	// A block lasts kRingFrames / kBlocks / rate seconds; expect two within
	// two and a half blocks plus a margin
	const unsigned blockMs = (kRingFrames / kBlocks) * 1000 / (_rate ? _rate : 22050);
	const uclock_t begin = uclock();
	const unsigned limitMs = blockMs * 5 / 2 + 100;
	while (g_isr.irqCount < 2) {
		if ((unsigned)((uclock() - begin) * 1000 / UCLOCKS_PER_SEC) > limitMs) {
			removeIrq();
			char note[96];
			snprintf(note, sizeof(note), "IRQ %d did not arrive, so no underrun protection", _irq);
			setError(note);
			return false;
		}
	}
	return true;
}

void SoundBlaster::removeIrq() {
	if (!_irqInstalled)
		return;
	_irqInstalled = false;

	outportb(_irqPort, inportb(_irqPort) | _irqBit); // mask the card's IRQ again
	_go32_dpmi_set_protected_mode_interrupt_vector(g_irqVector, &g_oldIrq);
	_go32_dpmi_free_iret_wrapper(&g_newIrq);
}

void SoundBlaster::stopPlayback() {
	if (_activeChannel >= 0) {
		const DmaPorts p = portsFor(_activeChannel);
		outportb(p.mask, 4 | (_activeChannel & 3)); // mask the DMA channel
	}
	if (_base) {
		if (!_bits16) {
			dspWrite(0xD0);                          // halt 8-bit DMA
			dspWrite(0xD3);                          // speaker off
		}
		resetDsp(_base);                             // a reset stops everything else
	}
	_activeChannel = -1;
}

bool SoundBlaster::init(unsigned requestedRate, int bits, bool useIrq) {
	if (_active)
		return true;

	_error[0] = 0;

	// Defaults, overridden by e.g. BLASTER=A220 I7 D1 H5 T6
	unsigned base = 0;
	_irq = -1;
	_dma = -1;
	_hdma = -1;

	const char *env = getenv("BLASTER");
	if (env) {
		const char *p = env;
		while (*p) {
			while (*p == ' ')
				p++;
			if (!*p)
				break;

			char tag = toupper((unsigned char)*p++);
			char *end;
			unsigned long v;
			switch (tag) {
			case 'A':
				v = strtoul(p, &end, 16);
				base = (unsigned)v;
				break;
			case 'I':
				v = strtoul(p, &end, 10);
				_irq = (int)v;
				break;
			case 'D':
				v = strtoul(p, &end, 10);
				_dma = (int)v;
				break;
			case 'H':
				v = strtoul(p, &end, 10);
				_hdma = (int)v;
				break;
			default:
				break;
			}
			while (*p && *p != ' ')
				p++;
		}
	}

	bool found = false;
	if (base)
		found = probe(base);
	for (unsigned b = 0x210; !found && b <= 0x280; b += 0x10)
		found = probe(b);

	if (!found) {
		setError("no Sound Blaster DSP answered at the BLASTER address or 0x210-0x280");
		return false;
	}

	// Auto-init DMA needs DSP 2.00 or later
	if (_dspMajor < 2) {
		setError("the DSP is older than 2.00 and has no auto-init DMA");
		return false;
	}

	if (!allocateBlock()) {
		setError("no conventional memory for the DMA buffer");
		return false;
	}

	// An SB16 can tell us its IRQ when BLASTER does not
	if ((_irq < 2 || _irq > 15) && _dspMajor >= 4)
		_irq = readSb16Irq();

	// Mask the card's IRQ until a handler is in place (without one it stays masked)
	if (_irq >= 2 && _irq <= 15) {
		_irqPort = (_irq < 8) ? 0x21 : 0xA1;
		_irqBit = 1 << (_irq & 7);
		_origPicMask = inportb(_irqPort);
		outportb(_irqPort, _origPicMask | _irqBit);
		_irqMasked = true;
	}

	g_active = this;
	if (!g_exitHookSet) {
		atexit(shutdownAtExit);
		g_exitHookSet = true;
	}

	bool running = false;

	// SB16: 16-bit stereo, unless 8-bit was asked for (DMA 5 if BLASTER names no H channel)
	if (bits != 8) {
		if (_dspMajor >= 4) {
			if (_hdma < 5 || _hdma > 7)
				_hdma = 5;
			if (start16(requestedRate) && dmaRunning()) {
				running = true;
			} else {
				stopPlayback();
				setError("16-bit mode did not start (the DMA channel does not move)");
			}
		} else if (bits == 16) {
			setError("16-bit mode needs an SB16 (DSP 4.xx)");
		}
	}

	if (!running && bits != 16) {
		// Use the 8-bit channel from BLASTER. Without one, try the usual
		// channel 1, then 3 and 0: only the card's own channel will move.
		static const int fallbackChannels[3] = { 1, 3, 0 };
		const int count = (_dma >= 0 && _dma <= 3) ? 1 : 3;
		for (int i = 0; i < count && !running; i++) {
			const int channel = (count == 1) ? _dma : fallbackChannels[i];
			if (start8(channel, requestedRate) && dmaRunning()) {
				running = true;
			} else {
				stopPlayback();
				char note[64];
				snprintf(note, sizeof(note), "8-bit DMA channel %d does not move", channel);
				setError(note);
			}
		}
	}

	if (!running) {
		shutdown();
		return false;
	}

	// The interrupt handler that keeps the card from replaying old sound
	// when the program is kept busy. Without it the driver still works.
	if (useIrq)
		installIrq();

	_active = true;
	return true;
}

void SoundBlaster::shutdown() {
	// Called for a failed init() too (not yet _active), to give things back
	if (_base && (_active || _activeChannel >= 0))
		stopPlayback();
	_active = false;

	removeIrq();
	if (_irqMasked) {
		outportb(_irqPort, _origPicMask);
		if (_cascadeTouched)
			outportb(0x21, _origMasterMask);
		_cascadeTouched = false;
		_irqMasked = false;
	}

	if (_dosSelector != -1) {
		__dpmi_free_dos_memory(_dosSelector);
		_dosSelector = -1;
	}

	if (g_active == this)
		g_active = nullptr;
}

unsigned SoundBlaster::getPlayPosition() {
	// Without the interrupt handler, acknowledge the (masked) interrupt here so
	// the card never waits on us. With it, the handler does that.
	if (!_irqInstalled) {
		(void)inportb(_base + 0xE);
		if (_bits16)
			(void)inportb(_base + 0xF);
	}

	const DmaPorts p = portsFor(_activeChannel);

	// The counter is read as two bytes and can change in between; read
	// until two consecutive reads agree.
	unsigned count = 0, previous = 0xFFFFFFFF;
	for (int i = 0; i < 8; i++) {
		outportb(p.clear, 0);
		unsigned lo = inportb(p.count);
		unsigned hi = inportb(p.count);
		count = lo | (hi << 8);
		if (count == previous)
			break;
		previous = count;
	}

	// count is "units left, minus one" (a unit is a byte, or a word on the
	// 16-bit channels); it wraps to 0xFFFF at terminal count
	unsigned remaining = (count + 1) * (_activeChannel >= 4 ? 2 : 1);
	if (remaining > _ringBytes)
		return 0;
	return (_ringBytes - remaining) / getFrameBytes();
}

void SoundBlaster::writeFrames(unsigned frameOffset, const void *data, unsigned frames) {
	if (!_active || frameOffset + frames > kRingFrames)
		return;
	const unsigned size = getFrameBytes();
	dosmemput(data, frames * size, _physAddr + frameOffset * size);
}
