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

// Game port joystick maths for dos-input.cpp; no port access, so unit tested on the host.
// Each axis is a timer counted in port reads; a centred stick gives about half the maximum.

#ifndef BACKENDS_PLATFORM_DOS_JOYSTICK_H
#define BACKENDS_PLATFORM_DOS_JOYSTICK_H

/** Buttons 1 and 2 of the first stick (port bits 4 and 5, clear while pressed) */
static inline int dosJoyButtons(unsigned portValue) {
	return (~portValue >> 4) & 3;
}

/** Are resting readings steady enough to be a stick? An empty port gives noise. */
static inline bool dosJoyCalibrated(int sumX, int sumY, int minX, int maxX, int minY, int maxY, int samples,
                                    int &centreX, int &centreY) {
	if (samples < 1)
		return false;
	centreX = sumX / samples;
	centreY = sumY / samples;
	if (centreX < 15 || centreY < 15)
		return false;
	if ((maxX - minX) * 4 > centreX || (maxY - minY) * 4 > centreY)
		return false;
	return true;
}

/**
 * Cursor pixels one axis moves in dtMs (at most 100): full deflection crosses
 * screenSize in a second, past a dead zone and a curve. fraction carries the
 * sub-pixel remainder, in thousandths, between calls.
 */
static inline int dosJoyAxisMove(int raw, int centre, int &fraction, int screenSize, int dtMs) {
	if (centre <= 0)
		return 0;

	// How far the stick is from the centre, in thousandths of its travel
	int v = (raw - centre) * 1000 / centre;
	if (v < -1000)
		v = -1000;
	if (v > 1000)
		v = 1000;

	int magnitude = v < 0 ? -v : v;
	if (magnitude < 200) {
		fraction = 0;
		return 0;
	}
	magnitude = (magnitude - 200) * 1000 / 800;   // 0..1000 beyond the dead zone
	magnitude = magnitude * magnitude / 1000;     // the curve
	v = (v < 0) ? -magnitude : magnitude;

	fraction += v * screenSize * dtMs / 1000;
	const int pixels = fraction / 1000;
	fraction -= pixels * 1000;
	return pixels;
}

#endif
