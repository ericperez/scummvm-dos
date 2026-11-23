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

#ifndef BACKENDS_PLATFORM_DOS_BIOSKEY_H
#define BACKENDS_PLATFORM_DOS_BIOSKEY_H

#include <stdint.h>

// Matching a scan code from the keyboard IRQ with the character KEYB (or the BIOS) queued
// for it, so every KEYB layout works; no DOS calls, so unit tested on the host

/**
 * Is this BIOS buffer word the one for this key press? A key that types nothing leaves
 * no word; keypad Enter and / are queued with 0xE0 as their scan code.
 */
static inline bool dosBiosWordMatches(uint16_t word, uint8_t scan, bool extended) {
	if (word == 0)
		return false;

	const uint8_t wordScan = (uint8_t)(word >> 8);
	if (extended && (scan == 0x1C || scan == 0x35))
		return wordScan == 0xE0;

	return wordScan == scan;
}

/** Is a control character typed by its own key (Backspace, Tab, Enter, Escape), not by Ctrl? */
static inline bool dosIsTextControl(uint8_t scan, bool extended, uint8_t character) {
	if (extended)
		return scan == 0x1C && character == 13; // keypad Enter

	switch (scan) {
	case 0x01:
		return character == 27; // Escape
	case 0x0E:
		return character == 8;  // Backspace
	case 0x0F:
		return character == 9;  // Tab
	case 0x1C:
		return character == 13; // Enter
	default:
		return false;
	}
}

/** Does this key never type a character? The BIOS queues 0xE0 for the gray keys. */
static inline bool dosKeyTypesNothing(uint8_t scan, bool extended) {
	if (extended)
		return !(scan == 0x1C || scan == 0x35);  // all but keypad Enter and /

	// Function keys (the keypad does type, with Num Lock on)
	return (scan >= 0x3B && scan <= 0x44) || scan == 0x57 || scan == 0x58;
}

#endif
