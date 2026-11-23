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

// This file talks directly to DOS/BIOS, so allow every system symbol.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <bios.h>
#include <dpmi.h>
#include <pc.h>
#include <stdlib.h>
#include <time.h>

#include "common/scummsys.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "backends/platform/dos/dos-input.h"
#include "backends/platform/dos/dos-joystick.h"
#include "backends/platform/dos/dos-keyboard.h"
#include "backends/platform/dos/dos-keymap.h"

static bool g_mouseAvailable = false;

// The DOS code page the characters the BIOS types are in (437 unless the system says otherwise)
static unsigned g_codePage = 437;

// The screen the mouse moves on; in mode 13h the driver counts two units a pixel across
static int g_screenW = 320;
static int g_screenH = 200;
static int g_xScale = 2;

// The driver has the wheel extension (CuteMouse 1.9+): function 3 returns the wheel in BH
static bool g_wheelAvailable = false;
static int g_wheelPending = 0;

// Where the mouse driver's cursor was at the last poll (the joystick moves it from there)
static int g_lastX = 0;
static int g_lastY = 0;

bool dosMouseInit(int screenWidth, int screenHeight) {
	g_screenW = screenWidth;
	g_screenH = screenHeight;
	g_xScale = (screenWidth == 320) ? 2 : 1;

	__dpmi_regs r;

	// Reset driver, AX = 0xFFFF on return when a driver is installed
	r.x.ax = 0x0000;
	__dpmi_int(0x33, &r);
	g_mouseAvailable = (r.x.ax == 0xFFFF);
	if (!g_mouseAvailable)
		return false;

	// Wheel support: AX = 0x0011 ("get capabilities") answers AX = 0x574D and sets bit 0 of CX
	g_wheelAvailable = false;
	g_wheelPending = 0;
	r.x.ax = 0x0011;
	r.x.cx = 0;
	__dpmi_int(0x33, &r);
	if (r.x.ax == 0x574D && (r.x.cx & 1))
		g_wheelAvailable = true;

	r.x.ax = 0x0007;
	r.x.cx = 0;
	r.x.dx = screenWidth * g_xScale - 1;
	__dpmi_int(0x33, &r);

	r.x.ax = 0x0008;
	r.x.cx = 0;
	r.x.dx = screenHeight - 1;
	__dpmi_int(0x33, &r);

	dosMouseSetPosition(screenWidth / 2, screenHeight / 2);

	// Throw away any button counts from before we started
	for (int i = 0; i < 3; i++) {
		r.x.ax = 0x0005;
		r.x.bx = i;
		__dpmi_int(0x33, &r);
		r.x.ax = 0x0006;
		r.x.bx = i;
		__dpmi_int(0x33, &r);
	}
	return true;
}

void dosMousePoll(int &x, int &y, int &buttons, int pressed[3], int released[3]) {
	for (int i = 0; i < 3; i++)
		pressed[i] = released[i] = 0;

	if (!g_mouseAvailable) {
		buttons = 0;
		return;
	}

	__dpmi_regs r;
	r.x.ax = 0x0003;
	__dpmi_int(0x33, &r);

	x = r.x.cx / g_xScale;
	y = r.x.dx;
	buttons = r.x.bx & 7;

	// BH is the wheel counter, a signed byte that the call resets: positive when
	// the wheel was turned towards the user, which scrolls down
	if (g_wheelAvailable)
		g_wheelPending += (int)(signed char)(r.x.bx >> 8);

	// Button press / release counters since the last time we asked
	// (AX = 5 / 6, BX = button; the call returns the count in BX and clears it)
	for (int i = 0; i < 3; i++) {
		r.x.ax = 0x0005;
		r.x.bx = i;
		__dpmi_int(0x33, &r);
		pressed[i] = r.x.bx;

		r.x.ax = 0x0006;
		r.x.bx = i;
		__dpmi_int(0x33, &r);
		released[i] = r.x.bx;
	}

	if (x < 0)
		x = 0;
	if (x > g_screenW - 1)
		x = g_screenW - 1;
	if (y < 0)
		y = 0;
	if (y > g_screenH - 1)
		y = g_screenH - 1;

	g_lastX = x;
	g_lastY = y;
}

int dosMouseTakeWheel() {
	// Positive for a turn away from the user (scroll up), as the events want it
	int n = -g_wheelPending;
	g_wheelPending = 0;
	return n;
}

void dosMouseSetPosition(int x, int y) {
	if (!g_mouseAvailable)
		return;

	__dpmi_regs r;
	r.x.ax = 0x0004;
	r.x.cx = x * g_xScale;
	r.x.dx = y;
	__dpmi_int(0x33, &r);
}

// Joystick on the game port, moving the mouse cursor

// Port 0x201: a write starts the axis timers, which run longer for more resistance;
// bits 0-3 read set while they run, and bits 4-7 are the buttons, clear when pressed
static const unsigned kGamePort = 0x201;

// How many reads of the port to wait for an axis before deciding it is not connected
static const int kJoyTimeout = 8000;

static bool g_joyPresent = false;
static int g_joyCenterX = 0, g_joyCenterY = 0; ///< timer length with the stick centred, in port reads
static uint32 g_joyLastMs = 0;
static int g_joyButtons = 0;
static int g_joyFracX = 0, g_joyFracY = 0;     ///< pixels not yet moved, in thousandths

/** One reading of the first stick. False if an axis timer never ran out. */
static bool joyReadOnce(int &x, int &y, int &buttons) {
	int nx = 0, ny = 0;
	unsigned s = 0xFF;

	outportb(kGamePort, 0xFF);
	for (int i = 1; i <= kJoyTimeout; i++) {
		s = inportb(kGamePort);
		if (s & 1)
			nx = i;
		if (s & 2)
			ny = i;
		if (!(s & 3))
			break;
	}
	if (s & 3)
		return false; // still running: nothing is attached to the axis

	x = nx;
	y = ny;
	buttons = dosJoyButtons(s);
	return true;
}

/** A reading free of the delays that an interrupt in the middle of it adds: the shorter of two. */
static bool joyRead(int &x, int &y, int &buttons) {
	int x1, y1, b1, x2, y2, b2;
	if (!joyReadOnce(x1, y1, b1))
		return false;
	// The timers need a moment to recharge
	uclock_t start = uclock();
	while (uclock() - start < UCLOCKS_PER_SEC / 500) {
	}
	if (!joyReadOnce(x2, y2, b2))
		return false;

	x = MIN(x1, x2);
	y = MIN(y1, y2);
	buttons = b2;
	return true;
}

bool dosJoystickInit() {
	// Only when asked for: an empty game port can make the cursor drift
	const char *wanted = getenv("SCUMMVM_DOS_JOYSTICK");
	if (!wanted || wanted[0] == '0')
		return false;

	// The stick must be centred now. Take a few readings: all must complete, and
	// agree with each other to within a quarter.
	int sumX = 0, sumY = 0, minX = 1 << 30, maxX = 0, minY = 1 << 30, maxY = 0;
	const int kSamples = 6;
	for (int i = 0; i < kSamples; i++) {
		int x, y, b;
		if (!joyRead(x, y, b))
			return false;
		sumX += x;
		sumY += y;
		minX = MIN(minX, x);
		maxX = MAX(maxX, x);
		minY = MIN(minY, y);
		maxY = MAX(maxY, y);
	}

	if (!dosJoyCalibrated(sumX, sumY, minX, maxX, minY, maxY, kSamples, g_joyCenterX, g_joyCenterY))
		return false;

	g_joyPresent = true;
	g_joyLastMs = 0;
	return true;
}

void dosJoystickUpdate(uint32 nowMs, int &buttons) {
	buttons = g_joyButtons;
	if (!g_joyPresent || !g_mouseAvailable)
		return;

	// About 30 times a second: each reading holds the program up for as long as the timers run
	uint32 dt = nowMs - g_joyLastMs;
	if (g_joyLastMs != 0 && dt < 33)
		return;
	g_joyLastMs = nowMs;
	if (dt > 100)
		dt = 100;

	int x, y, b;
	if (!joyRead(x, y, b))
		return; // a failed reading changes nothing

	g_joyButtons = b;
	buttons = b;

	// How far each axis moves the cursor (dos-joystick.h): a dead zone in the middle,
	// and a gentle curve so that small movements are precise
	int axes[2] = { x, y };
	int centres[2] = { g_joyCenterX, g_joyCenterY };
	int *fracs[2] = { &g_joyFracX, &g_joyFracY };
	int moves[2];
	for (int a = 0; a < 2; a++) {
		// A full deflection crosses the screen in a second
		moves[a] = dosJoyAxisMove(axes[a], centres[a], *fracs[a], a == 0 ? g_screenW : g_screenH, (int)dt);
	}

	if (moves[0] || moves[1])
		dosMouseSetPosition(CLIP(g_lastX + moves[0], 0, g_screenW - 1), CLIP(g_lastY + moves[1], 0, g_screenH - 1));
}

/** Keycode for the extended (scancode only) keys, or KEYCODE_INVALID. */
static Common::KeyCode extendedKey(int scan) {
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

/** Letter for a QWERTY scancode, used for Alt+letter combos. 0 if none. */
static char letterForScancode(int scan) {
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

// The BIOS fallback: presses only, no releases (see dosKeyboardNextKey())
static bool biosKeyboardPoll(Common::KeyState &ks) {
	// 0x11 = "enhanced keyboard: is a key waiting?", 0x10 = read it
	if (!bioskey(0x11))
		return false;

	int key = bioskey(0x10);
	int shift = bioskey(0x12);
	int scan = (key >> 8) & 0xFF;
	int ascii = key & 0xFF;

	uint8 flags = 0;
	if (shift & 0x03)
		flags |= Common::KBD_SHIFT;
	if (shift & 0x04)
		flags |= Common::KBD_CTRL;
	if (shift & 0x08)
		flags |= Common::KBD_ALT;

	Common::KeyCode kc = Common::KEYCODE_INVALID;
	int outAscii = 0;

	if (ascii == 0 || (ascii == 0xE0 && scan >= 0x47)) {
		// Extended key: arrows, function keys, Alt+letter, ...
		kc = extendedKey(scan);
		if (kc == Common::KEYCODE_INVALID) {
			char c = letterForScancode(scan);
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
		outAscii = dosCharToUnicode(g_codePage, (uint8)ascii);
	} else if (ascii >= 32 && ascii < 127) {
		outAscii = ascii;
		// Letters use the lower case keycode; the shift flag and the ASCII
		// value carry the case, as in the other backends.
		if (ascii >= 'A' && ascii <= 'Z')
			kc = (Common::KeyCode)(ascii - 'A' + 'a');
		else
			kc = (Common::KeyCode)ascii;
	}

	if (kc == Common::KEYCODE_INVALID && outAscii == 0)
		return false; // consumed a key we do not map; report nothing

	ks.keycode = kc;
	ks.ascii = outAscii;
	ks.flags = flags;
	return true;
}

// Scan codes (IRQ 1) to ScummVM key events

// Typewriter keys' keycodes: the ASCII of what they type unshifted, by scan code (0: none)
static const uint8 kTypewriterKeys[0x36] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/'
};

static Common::KeyCode keycodeForScan(uint8 scan, bool extended) {
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

	if (scan >= 0x3B && scan <= 0x44)
		return (Common::KeyCode)(Common::KEYCODE_F1 + (scan - 0x3B));

	if (scan < sizeof(kTypewriterKeys) && kTypewriterKeys[scan])
		return (Common::KeyCode)kTypewriterKeys[scan];

	return Common::KEYCODE_INVALID;
}

static uint8 flagsForModifiers(uint8 mods) {
	uint8 flags = 0;
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

// Each key as reported when it went down, for its release (Shift or the layout may change)
static struct {
	Common::KeyCode keycode;
	uint16 unicode;
	bool valid;
} g_pressedAs[2][128];

void dosKeyboardInit() {
	// The active code page: INT 21h, AX = 6601h returns it in BX
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x6601;
	if (__dpmi_int(0x21, &r) == 0 && !(r.x.flags & 1) && r.x.bx != 0)
		g_codePage = r.x.bx;

	// If this fails we use the BIOS, see dosKeyboardNextKey()
	dosKeyboardInstall();
}

unsigned dosKeyboardCodePage() {
	return g_codePage;
}

// BIOS fallback: the key pressed, whose release is reported on the next call
static bool g_releasePending = false;
static Common::KeyCode g_lastKeycode = Common::KEYCODE_INVALID;
static uint16 g_lastAscii = 0;
static byte g_lastFlags = 0;

bool dosKeyboardNextKey(DosKey &key) {
	if (dosKeyboardInstalled()) {
		DosKeyEvent e;
		while (dosKeyboardNext(e)) {
			const Common::KeyCode physical = keycodeForScan(e.scan, e.extended);
			uint8 mods = e.modifiers;
			Common::KeyCode kc = physical;
			uint16 unicode = 0;

			if (e.pressed) {
				// What the key types and is called depends on the layout: see dos-keymap.h
				const DosTypedKey typed = dosTypedKey(g_codePage, e.ascii, e.scan, e.extended, physical,
				                                      (e.modifiers & kDosModShift) != 0, e.rightAltOnly);
				kc = typed.keycode;
				unicode = typed.unicode;
				if (typed.dropAlt)
					mods &= ~kDosModAlt;
				if (kc == Common::KEYCODE_INVALID && unicode == 0)
					continue; // a key ScummVM has no name for, and that types nothing

				g_pressedAs[e.extended][e.scan].keycode = kc;
				g_pressedAs[e.extended][e.scan].unicode = unicode;
				g_pressedAs[e.extended][e.scan].valid = true;
			} else if (g_pressedAs[e.extended][e.scan].valid) {
				// The release is the key as it went down
				kc = g_pressedAs[e.extended][e.scan].keycode;
				unicode = g_pressedAs[e.extended][e.scan].unicode;
				g_pressedAs[e.extended][e.scan].valid = false;
			} else if (kc == Common::KEYCODE_INVALID) {
				continue;
			}

			key.state.keycode = kc;
			key.state.ascii = unicode;
			key.state.flags = flagsForModifiers(mods);
			key.pressed = e.pressed;
			key.repeat = e.repeat;
			return true;
		}
		return false;
	}

	// BIOS fallback: a press now, its release on the next call
	if (g_releasePending) {
		g_releasePending = false;
		key.state = Common::KeyState(g_lastKeycode, g_lastAscii, g_lastFlags);
		key.pressed = false;
		key.repeat = false;
		return true;
	}

	Common::KeyState ks;
	if (biosKeyboardPoll(ks)) {
		g_lastKeycode = ks.keycode;
		g_lastAscii = ks.ascii;
		g_lastFlags = ks.flags;
		g_releasePending = true;
		key.state = ks;
		key.pressed = true;
		key.repeat = false;
		return true;
	}
	return false;
}

// Self-test (SCUMMVM_DOS_KBDTEST=1): pairing key presses with KEYB's characters

namespace {

int g_kbdFailures = 0;
int g_kbdChecks = 0;

void kbdCheck(bool ok, const char *what) {
	g_kbdChecks++;
	if (!ok) {
		g_kbdFailures++;
		warning("KBDTEST: FAILED: %s", what);
	}
}

// A key press: the scan code to the decoder, the word (0: none) to the BIOS buffer
bool kbdPress(uint8 scan, bool extended, uint16 biosWord, DosKey &out) {
	if (biosWord)
		dosKeyboardStuffBiosKey(biosWord);
	if (extended)
		dosKeyboardInjectScan(0xE0);
	dosKeyboardInjectScan(scan);

	bool got = dosKeyboardNextKey(out);
	while (got && !out.pressed)
		got = dosKeyboardNextKey(out);
	return got;
}

// A modifier key going down or up, with its events taken
void kbdModifier(uint8 scanByte, bool extended) {
	if (extended)
		dosKeyboardInjectScan(0xE0);
	dosKeyboardInjectScan(scanByte);
	DosKey k;
	while (dosKeyboardNextKey(k)) {
	}
}

bool kbdRelease(uint8 scan, bool extended, DosKey &out) {
	if (extended)
		dosKeyboardInjectScan(0xE0);
	dosKeyboardInjectScan(scan | 0x80);
	return dosKeyboardNextKey(out) && !out.pressed;
}

} // End of anonymous namespace

void dosKeyboardSelfTest() {
	g_kbdFailures = 0;
	g_kbdChecks = 0;
	warning("KBDTEST: start, code page %u, hook %s", g_codePage, dosKeyboardInstalled() ? "installed" : "NOT installed");
	if (!dosKeyboardInstalled()) {
		warning("KBDTEST: FAILED: the keyboard hook is not installed");
		return;
	}

	DosKey k;
	// Settle: anything already typed
	while (dosKeyboardNextKey(k)) {
	}

	// German layout: the US 'y' key types 'z', and is named after it
	kbdCheck(kbdPress(0x15, false, (uint16)(0x15 << 8 | 'z'), k), "Y-position key press arrives");
	kbdCheck(k.state.keycode == Common::KEYCODE_z && k.state.ascii == 'z', "layout letter: key named z, types z");
	kbdCheck(kbdRelease(0x15, false, k) && k.state.keycode == Common::KEYCODE_z, "its release is named z too");

	// A French keyboard: the key that is 'q' on a US one types 'a'
	kbdCheck(kbdPress(0x10, false, (uint16)(0x10 << 8 | 'a'), k), "Q-position key press arrives");
	kbdCheck(k.state.keycode == Common::KEYCODE_a && k.state.ascii == 'a', "AZERTY: the Q-position key is a");
	kbdRelease(0x10, false, k);

	// An accented letter in code page 850 or 437 (0x82 is e-acute in both): Unicode U+00E9
	kbdCheck(kbdPress(0x28, false, (uint16)(0x28 << 8 | 0x82), k), "accented key press arrives");
	kbdCheck(k.state.ascii == 0x00E9, "e-acute comes out as U+00E9");
	kbdRelease(0x28, false, k);

	// A dead key types nothing by itself: the key is still reported, without a character
	kbdCheck(kbdPress(0x1B, false, 0, k), "dead key press arrives");
	kbdCheck(k.state.ascii == 0, "a dead key types no character");
	kbdRelease(0x1B, false, k);

	// AltGr (the right Alt) makes '@': text, not Alt+key
	kbdModifier(0x38, true); // right Alt down
	kbdCheck(kbdPress(0x10, false, (uint16)(0x10 << 8 | '@'), k), "AltGr+Q press arrives");
	kbdCheck(k.state.ascii == '@' && !(k.state.flags & Common::KBD_ALT), "AltGr character is text without Alt");
	kbdRelease(0x10, false, k);
	kbdModifier(0xB8, true); // right Alt up

	// Ctrl+C is a control character, not text
	kbdModifier(0x1D, false); // left Ctrl down
	kbdCheck(kbdPress(0x2E, false, (uint16)(0x2E << 8 | 3), k), "Ctrl+C press arrives");
	kbdCheck(k.state.ascii == 0 && (k.state.flags & Common::KBD_CTRL), "Ctrl+C types nothing and carries Ctrl");
	kbdRelease(0x2E, false, k);
	kbdModifier(0x9D, false); // left Ctrl up

	// Backspace and Enter are text control characters
	kbdCheck(kbdPress(0x0E, false, (uint16)(0x0E << 8 | 8), k) && k.state.ascii == 8, "Backspace types 8");
	kbdRelease(0x0E, false, k);
	kbdCheck(kbdPress(0x1C, true, (uint16)(0xE0 << 8 | 13), k) && k.state.ascii == 13 && k.state.keycode == Common::KEYCODE_KP_ENTER,
	         "keypad Enter types 13 and is the keypad key");
	kbdRelease(0x1C, true, k);

	// A gray arrow: the BIOS marks it with 0xE0 where the character goes, which is no character
	kbdCheck(kbdPress(0x48, true, (uint16)(0x48 << 8 | 0xE0), k) && k.state.ascii == 0 && k.state.keycode == Common::KEYCODE_UP,
	         "Up arrow types nothing");
	kbdRelease(0x48, true, k);

	// Nothing may be left in the BIOS buffer for the next test
	kbdCheck(!bioskey(0x11) || (bioskey(0x10), true), "the BIOS buffer can be emptied");

	if (g_kbdFailures == 0)
		warning("KBDTEST: OK %d checks, code page %u", g_kbdChecks, g_codePage);
	else
		warning("KBDTEST: %d FAILED", g_kbdFailures);
}
