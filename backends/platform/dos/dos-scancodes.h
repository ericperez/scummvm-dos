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


#ifndef BACKENDS_PLATFORM_DOS_SCANCODES_H
#define BACKENDS_PLATFORM_DOS_SCANCODES_H

#include <stdint.h>

#include "common/keyboard.h"

#include "backends/platform/dos/dos-keyboard.h"
#include "backends/platform/dos/dos-keymap.h"

// Scan codes (set 1) as the keyboard controller sends them to IRQ 1 and as the BIOS queues them,
// turned into key events and ScummVM key codes. No DOS calls, so unit tested on the host.

/** Modifier keys held down, tracked apart for the left and right keys. */
enum {
	kDosHeldLShift = 1 << 0,
	kDosHeldRShift = 1 << 1,
	kDosHeldLCtrl  = 1 << 2,
	kDosHeldRCtrl  = 1 << 3,
	kDosHeldLAlt   = 1 << 4,
	kDosHeldRAlt   = 1 << 5
};

/**
 * What the decoder of the keyboard controller's bytes remembers from one byte to the next.
 * Plain data, so it can be a global without a constructor: all zero is "no key down".
 */
struct DosScanState {
	uint8_t held;          ///< kDosHeld* of the modifier keys down now
	uint8_t locks;         ///< kDosModCaps, kDosModNum and kDosModScroll of the lock keys that are on
	bool down[2][128];     ///< [extended][scan code]: the key is down
	bool extPrefix;        ///< the last byte was the 0xE0 prefix
	int skip;              ///< bytes of the Pause key's sequence still to ignore
};

/** The kDosMod* flags of the modifier keys held and the lock keys that are on. */
static inline uint8_t dosScanModifiers(const DosScanState &state) {
	uint8_t m = state.locks;
	if (state.held & (kDosHeldLShift | kDosHeldRShift))
		m |= kDosModShift;
	if (state.held & (kDosHeldLCtrl | kDosHeldRCtrl))
		m |= kDosModCtrl;
	if (state.held & (kDosHeldLAlt | kDosHeldRAlt))
		m |= kDosModAlt;
	return m;
}

/**
 * Take one byte the keyboard controller sent.
 *
 * Prefix bytes (0xE0 for the gray keys, the 0xE1 sequence of Pause) and the fake Shift
 * presses some gray keys send around their own codes give no event.
 *
 * @param state  what was decoded so far; updated
 * @param code   the byte read from port 0x60
 * @param ev     filled in when a key went down or up, all but its character (ev.ascii is 0)
 * @return true if ev was filled in
 */
static inline bool dosScanDecode(DosScanState &state, uint8_t code, DosKeyEvent &ev) {
	if (state.skip) {
		// The rest of Pause's six-byte sequence
		state.skip--;
		return false;
	}
	if (code == 0xE1) {
		state.skip = 5;
		return false;
	}
	if (code == 0xE0) {
		state.extPrefix = true;
		return false;
	}

	const bool extended = state.extPrefix;
	state.extPrefix = false;

	const uint8_t scan = code & 0x7F;
	const bool pressed = !(code & 0x80);

	// Print Screen and a few others send "fake" Shift presses around
	// the real key; they are not the user pressing Shift.
	if (extended && (scan == 0x2A || scan == 0x36))
		return false;

	const bool repeat = pressed && state.down[extended][scan];
	state.down[extended][scan] = pressed;

	// Modifier keys
	uint8_t bit = 0;
	if (!extended && scan == 0x2A)
		bit = kDosHeldLShift;
	else if (!extended && scan == 0x36)
		bit = kDosHeldRShift;
	else if (scan == 0x1D)
		bit = extended ? kDosHeldRCtrl : kDosHeldLCtrl;
	else if (scan == 0x38)
		bit = extended ? kDosHeldRAlt : kDosHeldLAlt; // AltGr counts as Alt
	if (bit) {
		if (pressed)
			state.held |= bit;
		else
			state.held &= ~bit;
	}

	// Lock keys change on a press, not on its repeats
	if (pressed && !repeat && !extended) {
		if (scan == 0x3A)
			state.locks ^= kDosModCaps;
		else if (scan == 0x45)
			state.locks ^= kDosModNum;
		else if (scan == 0x46)
			state.locks ^= kDosModScroll;
	}

	ev.scan = scan;
	ev.extended = extended;
	ev.pressed = pressed;
	ev.repeat = repeat;
	ev.modifiers = dosScanModifiers(state);
	ev.ascii = 0;
	ev.rightAltOnly = (state.held & kDosHeldRAlt) && !(state.held & kDosHeldLAlt);
	return true;
}

/**
 * The lock keys that are on, from the BIOS keyboard flags (INT 16h, AH = 12h).
 *
 * @param biosFlags  the shift flags word the BIOS returned
 * @return kDosModCaps, kDosModNum and kDosModScroll
 */
static inline uint8_t dosLocksFromBiosFlags(int biosFlags) {
	uint8_t locks = 0;
	if (biosFlags & 0x40)
		locks |= kDosModCaps;
	if (biosFlags & 0x20)
		locks |= kDosModNum;
	if (biosFlags & 0x10)
		locks |= kDosModScroll;
	return locks;
}

/** Typewriter keys' key codes: the ASCII of what they type unshifted on a US keyboard, by scan code (0: none). */
static const uint8_t kDosTypewriterKeys[0x36] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/'
};

/**
 * The key code of a key by its place on the keyboard, as SDL names it on a US layout.
 *
 * @param scan      the scan code, without the release bit
 * @param extended  the key's codes came after the 0xE0 prefix
 * @return the key code, or KEYCODE_INVALID for a key ScummVM has no name for
 */
static inline Common::KeyCode dosKeycodeForScan(uint8_t scan, bool extended) {
	if (extended) {
		switch (scan) {
		case 0x1C:
			return Common::KEYCODE_KP_ENTER;
		case 0x1D:
			return Common::KEYCODE_RCTRL;
		case 0x35:
			return Common::KEYCODE_KP_DIVIDE;
		case 0x37:
			return Common::KEYCODE_PRINT;
		case 0x38:
			return Common::KEYCODE_RALT;
		case 0x47:
			return Common::KEYCODE_HOME;
		case 0x48:
			return Common::KEYCODE_UP;
		case 0x49:
			return Common::KEYCODE_PAGEUP;
		case 0x4B:
			return Common::KEYCODE_LEFT;
		case 0x4D:
			return Common::KEYCODE_RIGHT;
		case 0x4F:
			return Common::KEYCODE_END;
		case 0x50:
			return Common::KEYCODE_DOWN;
		case 0x51:
			return Common::KEYCODE_PAGEDOWN;
		case 0x52:
			return Common::KEYCODE_INSERT;
		case 0x53:
			return Common::KEYCODE_DELETE;
		default:
			return Common::KEYCODE_INVALID;
		}
	}

	switch (scan) {
	case 0x01:
		return Common::KEYCODE_ESCAPE;
	case 0x0E:
		return Common::KEYCODE_BACKSPACE;
	case 0x0F:
		return Common::KEYCODE_TAB;
	case 0x1C:
		return Common::KEYCODE_RETURN;
	case 0x1D:
		return Common::KEYCODE_LCTRL;
	case 0x2A:
		return Common::KEYCODE_LSHIFT;
	case 0x36:
		return Common::KEYCODE_RSHIFT;
	case 0x37:
		return Common::KEYCODE_KP_MULTIPLY;
	case 0x38:
		return Common::KEYCODE_LALT;
	case 0x39:
		return Common::KEYCODE_SPACE;
	case 0x3A:
		return Common::KEYCODE_CAPSLOCK;
	case 0x45:
		return Common::KEYCODE_NUMLOCK;
	case 0x46:
		return Common::KEYCODE_SCROLLOCK;
	case 0x47:
		return Common::KEYCODE_KP7;
	case 0x48:
		return Common::KEYCODE_KP8;
	case 0x49:
		return Common::KEYCODE_KP9;
	case 0x4A:
		return Common::KEYCODE_KP_MINUS;
	case 0x4B:
		return Common::KEYCODE_KP4;
	case 0x4C:
		return Common::KEYCODE_KP5;
	case 0x4D:
		return Common::KEYCODE_KP6;
	case 0x4E:
		return Common::KEYCODE_KP_PLUS;
	case 0x4F:
		return Common::KEYCODE_KP1;
	case 0x50:
		return Common::KEYCODE_KP2;
	case 0x51:
		return Common::KEYCODE_KP3;
	case 0x52:
		return Common::KEYCODE_KP0;
	case 0x53:
		return Common::KEYCODE_KP_PERIOD;
	case 0x57:
		return Common::KEYCODE_F11;
	case 0x58:
		return Common::KEYCODE_F12;
	default:
		break;
	}

	// F1..F10 are 0x3B..0x44
	if (scan >= 0x3B && scan <= 0x44)
		return (Common::KeyCode)(Common::KEYCODE_F1 + (scan - 0x3B));

	if (scan < sizeof(kDosTypewriterKeys) && kDosTypewriterKeys[scan])
		return (Common::KeyCode)kDosTypewriterKeys[scan];

	return Common::KEYCODE_INVALID;
}

/**
 * The ScummVM keyboard flags of the kDosMod* flags.
 *
 * @param mods  kDosMod* flags
 * @return Common::KBD_* flags
 */
static inline uint8_t dosFlagsForModifiers(uint8_t mods) {
	uint8_t flags = 0;
	if (mods & kDosModShift)
		flags |= Common::KBD_SHIFT;
	if (mods & kDosModCtrl)
		flags |= Common::KBD_CTRL;
	if (mods & kDosModAlt)
		flags |= Common::KBD_ALT;
	if (mods & kDosModCaps)
		flags |= Common::KBD_CAPS;
	if (mods & kDosModNum)
		flags |= Common::KBD_NUM;
	if (mods & kDosModScroll)
		flags |= Common::KBD_SCRL;
	return flags;
}

/**
 * The key code of a key the BIOS queued with no character (an "extended" key: arrows,
 * function keys, ...), by the scan code the BIOS gave it.
 *
 * @return the key code, or KEYCODE_INVALID
 */
static inline Common::KeyCode dosBiosExtendedKey(int scan) {
	switch (scan) {
	case 0x48:
		return Common::KEYCODE_UP;
	case 0x50:
		return Common::KEYCODE_DOWN;
	case 0x4B:
		return Common::KEYCODE_LEFT;
	case 0x4D:
		return Common::KEYCODE_RIGHT;
	case 0x47:
		return Common::KEYCODE_HOME;
	case 0x4F:
		return Common::KEYCODE_END;
	case 0x49:
		return Common::KEYCODE_PAGEUP;
	case 0x51:
		return Common::KEYCODE_PAGEDOWN;
	case 0x52:
		return Common::KEYCODE_INSERT;
	case 0x53:
		return Common::KEYCODE_DELETE;
	case 0x85:
		return Common::KEYCODE_F11;
	case 0x86:
		return Common::KEYCODE_F12;
	default:
		break;
	}

	// F1..F10 are 0x3B..0x44
	if (scan >= 0x3B && scan <= 0x44)
		return (Common::KeyCode)(Common::KEYCODE_F1 + (scan - 0x3B));

	return Common::KEYCODE_INVALID;
}

/**
 * The letter of a key on a US (QWERTY) keyboard, by its scan code: the BIOS queues
 * Alt+letter with no character, only the key's scan code.
 *
 * @return the lower case letter, or 0 for a key that is not a letter
 */
static inline char dosLetterForScan(int scan) {
	static const char row1[] = "qwertyuiop"; // 0x10..0x19
	static const char row2[] = "asdfghjkl";  // 0x1E..0x26
	static const char row3[] = "zxcvbnm";    // 0x2C..0x32

	if (scan >= 0x10 && scan <= 0x19)
		return row1[scan - 0x10];
	if (scan >= 0x1E && scan <= 0x26)
		return row2[scan - 0x1E];
	if (scan >= 0x2C && scan <= 0x32)
		return row3[scan - 0x2C];
	return 0;
}

/**
 * The key press of a word read from the BIOS keyboard buffer (INT 16h, AH = 10h), when
 * IRQ 1 could not be hooked. The BIOS reports presses only.
 *
 * @param key       the word: scan code in the high byte, character (in the code page) in the low byte
 * @param shift     the shift flags (INT 16h, AH = 12h)
 * @param codePage  the DOS code page the character is in
 * @param ks        filled in with the key
 * @return false for a key ScummVM has no name for and that types nothing (ks is left alone)
 */
static inline bool dosBiosKeyState(int key, int shift, unsigned codePage, Common::KeyState &ks) {
	const int scan = (key >> 8) & 0xFF;
	const int ascii = key & 0xFF;

	byte flags = 0;
	if (shift & 0x03)
		flags |= Common::KBD_SHIFT;
	if (shift & 0x04)
		flags |= Common::KBD_CTRL;
	if (shift & 0x08)
		flags |= Common::KBD_ALT;

	Common::KeyCode kc = Common::KEYCODE_INVALID;
	uint16 outAscii = 0;

	if (ascii == 0 || (ascii == 0xE0 && scan >= 0x47)) {
		// Extended key: arrows, function keys, Alt+letter, ...
		kc = dosBiosExtendedKey(scan);
		if (kc == Common::KEYCODE_INVALID) {
			const char c = dosLetterForScan(scan);
			if (c) {
				kc = (Common::KeyCode)c;
				flags |= Common::KBD_ALT;
			}
		}
	} else if (ascii == 8) {
		kc = Common::KEYCODE_BACKSPACE;
		outAscii = 8;
	} else if (ascii == 9) {
		kc = Common::KEYCODE_TAB;
		outAscii = 9;
	} else if (ascii == 13) {
		kc = Common::KEYCODE_RETURN;
		outAscii = 13;
	} else if (ascii == 27) {
		kc = Common::KEYCODE_ESCAPE;
		outAscii = 27;
	} else if (ascii >= 1 && ascii <= 26) {
		// Ctrl+A .. Ctrl+Z
		kc = (Common::KeyCode)('a' + ascii - 1);
		flags |= Common::KBD_CTRL;
	} else if (ascii >= 128) {
		// A character of the layout that ASCII has not: the code page says which
		outAscii = dosCharToUnicode(codePage, (uint8_t)ascii);
	} else if (ascii >= 32 && ascii < 127) {
		outAscii = ascii;
		// Letters use the lower case keycode; the shift flag and the ASCII
		// value carry the case, as in the other backends.
		kc = dosKeycodeForChar(ascii);
	}

	if (kc == Common::KEYCODE_INVALID && outAscii == 0)
		return false;

	ks.keycode = kc;
	ks.ascii = outAscii;
	ks.flags = flags;
	return true;
}

#endif
