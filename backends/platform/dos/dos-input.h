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

#ifndef BACKENDS_PLATFORM_DOS_INPUT_H
#define BACKENDS_PLATFORM_DOS_INPUT_H

#include "common/keyboard.h"

/**
 * Initialise the INT 33h mouse driver for a screen of this size, after the
 * video mode is set (the driver's range depends on the mode).
 *
 * @return true if a mouse driver is present.
 */
bool dosMouseInit(int screenWidth, int screenHeight);

/**
 * Read the mouse position (screen pixels) and buttons (bit 0 left, 1 right, 2 middle).
 * pressed[i] / released[i] count button i's presses and releases since the last
 * call, to catch a click that happened between two polls.
 */
void dosMousePoll(int &x, int &y, int &buttons, int pressed[3], int released[3]);

/** Move the mouse driver's cursor position (screen pixels). */
void dosMouseSetPosition(int x, int y);

/** Wheel clicks since the last call, positive to scroll up (0 without driver support) */
int dosMouseTakeWheel();

/**
 * Look for a centred joystick on the game port (only with SCUMMVM_DOS_JOYSTICK=1).
 *
 * @return true if a joystick was found.
 */
bool dosJoystickInit();

/**
 * Read the joystick (about 30 times a second at most), move the mouse driver's
 * cursor, and set buttons (bit 0 left, bit 1 right). Call before dosMousePoll().
 */
void dosJoystickUpdate(uint32 nowMs, int &buttons);

/** A key going down, going up, or repeating while held. */
struct DosKey {
	Common::KeyState state;
	bool pressed;    ///< true for a key going down, false for one released
	bool repeat;     ///< going down again because it is held (typematic)
};

/**
 * Start reading the keyboard through IRQ 1, or through the BIOS if that fails
 * (presses only: each release is then made up straight after its press).
 */
void dosKeyboardInit();

/** The DOS code page the typed characters are in (see dosKeyboardInit()). */
unsigned dosKeyboardCodePage();

/** Testing aid (SCUMMVM_DOS_KBDTEST=1): checks the layout characters, logging KBDTEST lines */
void dosKeyboardSelfTest();

/** Take the next key event, if any. */
bool dosKeyboardNextKey(DosKey &key);

#endif
