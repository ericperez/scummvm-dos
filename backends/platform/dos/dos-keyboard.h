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

#ifndef BACKENDS_PLATFORM_DOS_KEYBOARD_H
#define BACKENDS_PLATFORM_DOS_KEYBOARD_H

#include <stdint.h>

/**
 * Keyboard input from IRQ 1, which reports key releases (the BIOS does not).
 *
 * The interrupt handler only queues the scan codes. dosKeyboardNext(), called
 * from the main program, decodes them and pairs each press with the character
 * the BIOS (or KEYB) typed for it.
 */

enum {
	kDosModShift  = 1 << 0,
	kDosModCtrl   = 1 << 1,
	kDosModAlt    = 1 << 2,
	kDosModCaps   = 1 << 3,   ///< Caps Lock is on
	kDosModNum    = 1 << 4,   ///< Num Lock is on
	kDosModScroll = 1 << 5    ///< Scroll Lock is on
};

struct DosKeyEvent {
	uint8_t scan;        ///< Scan code (set 1) of the key, without the release bit
	bool extended;       ///< The key's codes are prefixed by 0xE0 (arrows, right Ctrl, ...)
	bool pressed;        ///< true when going down, false when released
	bool repeat;         ///< Going down while already down: typematic repeat
	uint8_t modifiers;   ///< kDosMod* in force after this event
	uint8_t ascii;       ///< What the BIOS (KEYB's layout) typed for this press, in the code page; 0 if none
	bool rightAltOnly;   ///< Of the Alt keys, only the right one is held (AltGr)
};

/**
 * Hook IRQ 1, chaining to the BIOS handler (whose buffer dosKeyboardNext() empties).
 *
 * @return true if the hook is installed.
 */
bool dosKeyboardInstall();

/**
 * Testing aid: act as if the keyboard controller sent this byte. Put what the
 * BIOS would type in first, with dosKeyboardStuffBiosKey().
 */
void dosKeyboardInjectScan(uint8_t byte);

/** Testing aid: put a word (scan code << 8 | character) in the BIOS keyboard buffer. */
bool dosKeyboardStuffBiosKey(uint16_t word);

/** Unhook IRQ 1. Done automatically at exit. Safe to call twice. */
void dosKeyboardRemove();

/** True once dosKeyboardInstall() has succeeded. */
bool dosKeyboardInstalled();

/**
 * Take the next decoded key event, if any.
 *
 * @return true if ev was filled in.
 */
bool dosKeyboardNext(DosKeyEvent &ev);

#endif
