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

#ifndef BACKENDS_PLATFORM_DOS_MODES_H
#define BACKENDS_PLATFORM_DOS_MODES_H

#include <stdint.h>
#include <stdlib.h>

// Choosing the launcher's resolution from the card's VESA 256 colour modes and, when the
// monitor answers DDC, its EDID; no DOS calls, so unit tested on the host

struct DosModeSize {
	int w, h;
};

/** Read "800x600" (any case of the x, spaces ignored). */
static inline bool dosParseSize(const char *s, int &w, int &h) {
	if (!s)
		return false;

	char *end = nullptr;
	long a = strtol(s, &end, 10);
	if (end == s || (*end != 'x' && *end != 'X'))
		return false;
	const char *p = end + 1;
	long b = strtol(p, &end, 10);
	if (end == p || a < 1 || b < 1 || a > 8192 || b > 8192)
		return false;

	w = (int)a;
	h = (int)b;
	return true;
}

/**
 * The best resolution in a 128 byte EDID block: the first detailed timing (a panel's
 * native mode), else the largest established or standard timing; false if none
 */
static inline bool dosEdidSize(const uint8_t *e, int &w, int &h) {
	static const uint8_t header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	for (int i = 0; i < 8; i++) {
		if (e[i] != header[i])
			return false;
	}

	// First detailed timing; a zero pixel clock means the slot holds something else
	if (e[54] || e[55]) {
		int ha = e[56] | ((e[58] & 0xF0) << 4);
		int va = e[59] | ((e[61] & 0xF0) << 4);
		if (ha >= 320 && va >= 200 && ha <= 8192 && va <= 8192) {
			w = ha;
			h = va;
			return true;
		}
	}

	int bestW = 0, bestH = 0;
#define DOS_EDID_CONSIDER(cw, ch) \
	do { if ((long)(cw) * (ch) > (long)bestW * bestH) { bestW = (cw); bestH = (ch); } } while (0)

	// Established timings
	if (e[35] & 0x20) DOS_EDID_CONSIDER(640, 480);
	if (e[35] & 0x10) DOS_EDID_CONSIDER(640, 480);
	if (e[35] & 0x08) DOS_EDID_CONSIDER(640, 480);
	if (e[35] & 0x04) DOS_EDID_CONSIDER(640, 480);
	if (e[35] & 0x03) DOS_EDID_CONSIDER(800, 600);
	if (e[36] & 0xC0) DOS_EDID_CONSIDER(800, 600);
	if (e[36] & 0x20) DOS_EDID_CONSIDER(832, 624);
	if (e[36] & 0x1E) DOS_EDID_CONSIDER(1024, 768);
	if (e[36] & 0x01) DOS_EDID_CONSIDER(1280, 1024);

	// Standard timings: eight pairs of bytes, 0x0101 meaning "unused"
	for (int i = 0; i < 8; i++) {
		unsigned b0 = e[38 + i * 2], b1 = e[39 + i * 2];
		if (b0 == 0x01 && b1 == 0x01)
			continue;
		if (b0 == 0x00 || b0 == 0xFF)
			continue;

		int sw = ((int)b0 + 31) * 8;
		int sh = 0;
		switch (b1 >> 6) {
		case 0: // 16:10 (1:1 before EDID 1.3, which no one built for)
			sh = sw * 10 / 16;
			break;
		case 1:
			sh = sw * 3 / 4;
			break;
		case 2:
			sh = sw * 4 / 5;
			break;
		default:
			sh = sw * 9 / 16;
			break;
		}
		if (sw >= 320 && sh >= 200)
			DOS_EDID_CONSIDER(sw, sh);
	}
#undef DOS_EDID_CONSIDER

	if (!bestW)
		return false;

	w = bestW;
	h = bestH;
	return true;
}

/** True if w:h is within 2.5% of num:den. */
static inline bool dosAspectMatches(int w, int h, int num, int den) {
	long diff = (long)w * den - (long)h * num;
	if (diff < 0)
		diff = -diff;
	return diff * 1000 <= 25L * h * num;
}

static inline bool dosPickLargest(const DosModeSize *modes, int count, int limW, int limH,
                                  int num, int den, DosModeSize &out) {
	bool found = false;
	for (int i = 0; i < count; i++) {
		const DosModeSize &m = modes[i];
		if (m.w < 640 || m.h < 400)
			continue; // the launcher at 320x200 is the VGA mode, not a VESA one
		if (m.w > limW || m.h > limH)
			continue;
		if (!dosAspectMatches(m.w, m.h, num, den))
			continue;

		if (!found || (long)m.w * m.h > (long)out.w * out.h) {
			out = m;
			found = true;
		}
	}
	return found;
}

/**
 * The launcher's resolution: the largest of the card's modes within maxW x maxH (and the
 * monitor's size, if known) in the monitor's shape, else 4:3; false if none qualifies
 */
static inline bool dosChooseGuiMode(const DosModeSize *modes, int count, int maxW, int maxH,
                                    bool haveNative, int nativeW, int nativeH, DosModeSize &out) {
	int limW = maxW, limH = maxH;
	if (haveNative) {
		if (nativeW < limW)
			limW = nativeW;
		if (nativeH < limH)
			limH = nativeH;

		// A mode of the monitor's own shape first
		if (dosPickLargest(modes, count, limW, limH, nativeW, nativeH, out))
			return true;
	}

	// Then 4:3, a little stretched on a panel of another shape, rather than 320x200
	return dosPickLargest(modes, count, limW, limH, 4, 3, out);
}

#endif
