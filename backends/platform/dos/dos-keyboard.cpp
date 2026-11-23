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

// This file hooks an interrupt and talks to the keyboard controller, so allow
// every system symbol.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <bios.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdlib.h>
#include <string.h>

#include "backends/platform/dos/dos-bioskey.h"
#include "backends/platform/dos/dos-keyboard.h"

// The interrupt side: runs inside IRQ 1, so only locked memory and no library calls

static volatile uint8_t g_ring[256];
static volatile uint8_t g_head = 0;   ///< written by the interrupt handler
static volatile uint8_t g_tail = 0;   ///< written by the main program

static void keyboardIrqHandler() {
	// The chained BIOS handler acknowledges the interrupt, we only read
	g_ring[g_head++] = inportb(0x60); // 8-bit index: wraps by itself
}

// Marks the end of keyboardIrqHandler() so its size can be locked
static void keyboardIrqHandlerEnd() {
}

static _go32_dpmi_seginfo g_oldHandler;
static _go32_dpmi_seginfo g_newHandler;
static bool g_installed = false;
static bool g_exitHandlerSet = false;

// Lock keys that are on (kDosModCaps | kDosModNum | kDosModScroll)
static uint8_t g_locks = 0;

static void removeAtExit() {
	dosKeyboardRemove();
}

bool dosKeyboardInstall() {
	if (g_installed)
		return true;

	if (_go32_dpmi_get_protected_mode_interrupt_vector(9, &g_oldHandler) != 0)
		return false;

	// The code and data touched by the handler must never be paged out
	if (_go32_dpmi_lock_code((void *)keyboardIrqHandler,
	                         (unsigned long)keyboardIrqHandlerEnd - (unsigned long)keyboardIrqHandler + 16) != 0)
		return false;
	if (_go32_dpmi_lock_data(const_cast<uint8_t *>(g_ring), sizeof(g_ring)) != 0)
		return false;
	if (_go32_dpmi_lock_data(const_cast<uint8_t *>(&g_head), sizeof(g_head)) != 0)
		return false;

	g_head = g_tail = 0;

	// Start from the lock keys' real state (Num Lock is usually on at boot)
	int bios = bioskey(0x12);
	g_locks = 0;
	if (bios & 0x40)
		g_locks |= kDosModCaps;
	if (bios & 0x20)
		g_locks |= kDosModNum;
	if (bios & 0x10)
		g_locks |= kDosModScroll;

	memset(&g_newHandler, 0, sizeof(g_newHandler));
	g_newHandler.pm_offset = (unsigned long)keyboardIrqHandler;
	g_newHandler.pm_selector = _go32_my_cs();

	// Chain: ours runs first, then the previous handler (the BIOS)
	if (_go32_dpmi_chain_protected_mode_interrupt_vector(9, &g_newHandler) != 0)
		return false;

	g_installed = true;

	if (!g_exitHandlerSet) {
		atexit(removeAtExit);
		g_exitHandlerSet = true;
	}
	return true;
}

void dosKeyboardRemove() {
	if (!g_installed)
		return;
	g_installed = false;

	_go32_dpmi_set_protected_mode_interrupt_vector(9, &g_oldHandler);
}

bool dosKeyboardInstalled() {
	return g_installed;
}

void dosKeyboardInjectScan(uint8_t byte) {
	g_ring[g_head++] = byte;
}

bool dosKeyboardStuffBiosKey(uint16_t word) {
	// INT 16h, AH = 05h: store a keystroke in the buffer (CH = scan code, CL = character)
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.h.ah = 0x05;
	r.x.cx = word;
	__dpmi_int(0x16, &r);
	return r.h.al == 0;
}

// The decoding side, run from the main program

// Modifier keys held right now (left and right tracked apart)
enum {
	kLShift = 1 << 0, kRShift = 1 << 1,
	kLCtrl = 1 << 2, kRCtrl = 1 << 3,
	kLAlt = 1 << 4, kRAlt = 1 << 5
};

static uint8_t g_held = 0;
static bool g_down[2][128];          ///< [extended][scan code]
static bool g_extPrefix = false;
static int g_skip = 0;               ///< bytes of the Pause sequence still to ignore
static bool g_needBiosDrain = false;

static uint8_t currentModifiers() {
	uint8_t m = g_locks;
	if (g_held & (kLShift | kRShift))
		m |= kDosModShift;
	if (g_held & (kLCtrl | kRCtrl))
		m |= kDosModCtrl;
	if (g_held & (kLAlt | kRAlt))
		m |= kDosModAlt;
	return m;
}

static void drainBiosBuffer() {
	// The BIOS saw every key too (we chain to it) and queued it; nobody
	// reads that queue, so empty it or it fills up and starts beeping.
	while (bioskey(0x11))
		bioskey(0x10);
}

bool dosKeyboardNext(DosKeyEvent &ev) {
	while (g_tail != g_head) {
		uint8_t b = g_ring[g_tail++];

		if (g_skip) {
			// The rest of Pause's six-byte sequence
			g_skip--;
			continue;
		}
		if (b == 0xE1) {
			g_skip = 5;
			continue;
		}
		if (b == 0xE0) {
			g_extPrefix = true;
			continue;
		}

		bool extended = g_extPrefix;
		g_extPrefix = false;

		uint8_t scan = b & 0x7F;
		bool pressed = !(b & 0x80);

		// Print Screen and a few others send "fake" Shift presses around
		// the real key; they are not the user pressing Shift.
		if (extended && (scan == 0x2A || scan == 0x36))
			continue;

		bool repeat = false;
		bool wasDown = g_down[extended][scan];
		if (pressed) {
			repeat = wasDown;
			g_down[extended][scan] = true;
			g_needBiosDrain = true;
		} else {
			g_down[extended][scan] = false;
		}

		// Modifier and lock keys
		uint8_t bit = 0;
		if (!extended && scan == 0x2A) bit = kLShift;
		else if (!extended && scan == 0x36) bit = kRShift;
		else if (!extended && scan == 0x1D) bit = kLCtrl;
		else if (extended && scan == 0x1D) bit = kRCtrl;
		else if (!extended && scan == 0x38) bit = kLAlt;
		else if (extended && scan == 0x38) bit = kRAlt;   // AltGr counts as Alt
		if (bit) {
			if (pressed)
				g_held |= bit;
			else
				g_held &= ~bit;
		}

		if (pressed && !repeat && !extended) {
			if (scan == 0x3A)
				g_locks ^= kDosModCaps;
			else if (scan == 0x45)
				g_locks ^= kDosModNum;
			else if (scan == 0x46)
				g_locks ^= kDosModScroll;
		}

		ev.scan = scan;
		ev.extended = extended;
		ev.pressed = pressed;
		ev.repeat = repeat;
		ev.modifiers = currentModifiers();
		ev.rightAltOnly = (g_held & kRAlt) && !(g_held & kLAlt);

		// The character is the word at the front of the BIOS buffer, if it is this key's
		// (KEYB's layout; dead keys and Alt+letter type nothing and leave no word)
		ev.ascii = 0;
		if (pressed) {
			const unsigned word = (unsigned)bioskey(0x11); // looks, does not take
			if (dosBiosWordMatches((uint16_t)word, scan, extended)) {
				bioskey(0x10); // now take it
				ev.ascii = (uint8_t)(word & 0xFF);
			}
		}
		return true;
	}

	if (g_needBiosDrain) {
		g_needBiosDrain = false;
		drainBiosBuffer();
	}
	return false;
}
