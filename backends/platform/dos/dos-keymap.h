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

#ifndef BACKENDS_PLATFORM_DOS_KEYMAP_H
#define BACKENDS_PLATFORM_DOS_KEYMAP_H

#include <stdint.h>

#include "common/keyboard.h"

#include "backends/platform/dos/dos-bioskey.h"
#include "backends/platform/dos/dos-codepages.h"

// What a key press types (KeyState::ascii, in Unicode) and its name (KeyState::keycode,
// following the layout as under SDL), from the BIOS buffer character in the DOS code page
// in use. No DOS calls, so unit tested on the host.

/** The Unicode character of a DOS character in a code page (ASCII is itself; an unknown page is read as 437). */
static inline uint16_t dosCharToUnicode(unsigned codePage, uint8_t ch) {
	if (ch < 0x80)
		return ch;

	unsigned index = 0; // 437
	for (unsigned i = 0; i < kDosCodePageCount; i++) {
		if (kDosCodePageNumbers[i] == codePage) {
			index = i;
			break;
		}
	}
	return kDosCodePageTable[index][ch - 0x80];
}

/** The key code ScummVM names a typed ASCII character by: its lower case, since a key has no case. */
static inline Common::KeyCode dosKeycodeForChar(uint16_t unicode) {
	if (unicode >= 'A' && unicode <= 'Z')
		return (Common::KeyCode)(unicode - 'A' + 'a');
	if (unicode >= 32 && unicode < 127)
		return (Common::KeyCode)unicode;
	return Common::KEYCODE_INVALID;
}

/** What a key press types and is called (see dosTypedKey()) */
struct DosTypedKey {
	uint16_t unicode;         ///< The character typed (KeyState::ascii), 0 if none
	Common::KeyCode keycode;  ///< The key's name (KeyState::keycode)
	bool dropAlt;             ///< The Alt held was AltGr making the character: do not report Alt
};

/**
 * What a key press types and is called.
 *
 * @param codePage   The DOS code page in use
 * @param biosChar   The character the BIOS queued for this press, or 0 if none
 * @param scan       The key's scan code
 * @param extended   The key's codes are prefixed by 0xE0
 * @param physical   The key's name from its place on the keyboard (a US keyboard's), or KEYCODE_INVALID
 * @param shift      Shift is held
 * @param rightAltOnly  Of the Alt keys, only the right one (AltGr on most layouts) is held
 */
static inline DosTypedKey dosTypedKey(unsigned codePage, uint8_t biosChar, uint8_t scan, bool extended,
                                      Common::KeyCode physical, bool shift, bool rightAltOnly) {
	DosTypedKey out;
	out.unicode = 0;
	out.keycode = physical;
	out.dropAlt = false;

	uint16_t unicode = 0;
	if (!dosKeyTypesNothing(scan, extended)) {
		if (biosChar >= 32 && biosChar != 127)
			unicode = dosCharToUnicode(codePage, biosChar);
		else if (biosChar != 0 && dosIsTextControl(scan, extended, biosChar))
			unicode = biosChar;
	}
	out.unicode = unicode;

	// A key typing printable ASCII is named by its character, as the layout has it, except
	// a shifted symbol ('!' of Shift+1), which keeps its key's name
	const Common::KeyCode byChar = dosKeycodeForChar(unicode);
	const bool keypad = !extended && (scan == 0x37 || (scan >= 0x47 && scan <= 0x53)); // they keep their own names
	if (byChar != Common::KEYCODE_INVALID && !extended && !keypad) {
		const bool letterOrDigit = (unicode >= 'A' && unicode <= 'Z') || (unicode >= 'a' && unicode <= 'z') ||
		                           (unicode >= '0' && unicode <= '9');
		if (!shift || letterOrDigit || physical == Common::KEYCODE_INVALID)
			out.keycode = byChar;
	}

	// AltGr makes characters (@, {, \ ...) on many layouts: that is text, not an Alt shortcut
	out.dropAlt = rightAltOnly && unicode >= 32;
	return out;
}

#endif
