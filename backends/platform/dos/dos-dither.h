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

#ifndef BACKENDS_PLATFORM_DOS_DITHER_H
#define BACKENDS_PLATFORM_DOS_DITHER_H

#include <stdint.h>

// High colour (15/16 bit) games on a 256 colour screen, for cards without the video memory
// for a high colour mode: a fixed 6x7x6 colour cube with 4x4 Bayer ordered dithering, and
// plain (undithered) tables for the mouse cursor. No DOS calls, so unit tested on the host.

enum {
	kDitherRed = 6,                                            ///< levels of red
	kDitherGreen = 7,                                          ///< levels of green
	kDitherBlue = 6,                                           ///< levels of blue
	kDitherColours = kDitherRed * kDitherGreen * kDitherBlue,  ///< 252 palette entries are used
	kDitherMulRed = kDitherGreen * kDitherBlue,                ///< palette index step of one red level
	kDitherMulGreen = kDitherBlue,
	kDitherMulBlue = 1
};

/** The 4x4 Bayer matrix, row by row; a pixel at (x, y) uses entry (y & 3) * 4 + (x & 3). */
static const uint8_t dosBayer4[16] = {
	0,  8,  2, 10,
	12,  4, 14,  6,
	3, 11,  1,  9,
	15,  7, 13,  5
};

/** Widen a channel value of 'bits' bits to 0..255 by repeating its top bits */
static inline int dosExpand8(int v, int bits) {
	if (bits >= 8)
		return v;
	int out = v;
	int have = bits;
	while (have < 8) {
		out = (out << have) | out;
		have *= 2;
	}
	return out >> (have - 8);
}

/** The level (0 .. levels-1) of an 8-bit channel value for a matrix entry 0..15. */
static inline int dosDitherLevel(int v8, int levels, int cell) {
	// Float is fine here: the tables are built once, not per pixel
	double x = v8 * (levels - 1) / 255.0 + (cell + 0.5) / 16.0;
	int lvl = (int)x;
	return lvl > levels - 1 ? levels - 1 : lvl;
}

/** The nearest level, no pattern. */
static inline int dosPlainLevel(int v8, int levels) {
	return (int)(v8 * (levels - 1) / 255.0 + 0.5);
}

/**
 * One channel's table for all 16 matrix cells: table[cell * 256 + value] is level * mul,
 * and the three channels' entries add up to the palette index
 */
static inline void dosDitherChannelTable(uint8_t *table, int bits, int levels, int mul) {
	const int count = 1 << bits;
	for (int cell = 0; cell < 16; cell++) {
		for (int v = 0; v < 256; v++) {
			int v8 = dosExpand8(v < count ? v : count - 1, bits);
			table[cell * 256 + v] = (uint8_t)(dosDitherLevel(v8, levels, dosBayer4[cell]) * mul);
		}
	}
}

/** The same without the pattern: table[value]. */
static inline void dosPlainChannelTable(uint8_t *table, int bits, int levels, int mul) {
	const int count = 1 << bits;
	for (int v = 0; v < 256; v++) {
		int v8 = dosExpand8(v < count ? v : count - 1, bits);
		table[v] = (uint8_t)(dosPlainLevel(v8, levels) * mul);
	}
}

/** The palette (8 bits per component): the 252 colour cube entries, then four unused greys */
static inline void dosDitherPalette(uint8_t pal[256][3]) {
	for (int i = 0; i < 256; i++) {
		if (i < kDitherColours) {
			int ri = i / kDitherMulRed;
			int gi = (i % kDitherMulRed) / kDitherMulGreen;
			int bi = i % kDitherMulGreen;
			pal[i][0] = (uint8_t)((ri * 255 + (kDitherRed - 1) / 2) / (kDitherRed - 1));
			pal[i][1] = (uint8_t)((gi * 255 + (kDitherGreen - 1) / 2) / (kDitherGreen - 1));
			pal[i][2] = (uint8_t)((bi * 255 + (kDitherBlue - 1) / 2) / (kDitherBlue - 1));
		} else {
			uint8_t grey = (uint8_t)((i - kDitherColours + 1) * 255 / 4);
			pal[i][0] = pal[i][1] = pal[i][2] = grey;
		}
	}
}

/**
 * Dither columns x0 to x1 of a line into d (s[0] is column x0). rt/gt/bt are the channel
 * tables at (y & 3) * 1024; rs/gs/bs and rm/gm/bm take the channels out of a pixel.
 * Unrolled by four; matches dosDitherRowReference byte for byte.
 */
static inline void dosDitherRow(uint8_t *d, const uint16_t *s, int x0, int x1,
                                const uint8_t *rt, const uint8_t *gt, const uint8_t *bt,
                                int rs, int gs, int bs, unsigned rm, unsigned gm, unsigned bm) {
	int x = x0;
	for (; x < x1 && (x & 3); x++, s++) {
		const unsigned p = *s;
		const int c = (x & 3) * 256;
		d[x] = rt[c + ((p >> rs) & rm)] + gt[c + ((p >> gs) & gm)] + bt[c + ((p >> bs) & bm)];
	}
	for (; x + 4 <= x1; x += 4, s += 4) {
		const unsigned p0 = s[0], p1 = s[1], p2 = s[2], p3 = s[3];
		d[x]     = rt[((p0 >> rs) & rm)]       + gt[((p0 >> gs) & gm)]       + bt[((p0 >> bs) & bm)];
		d[x + 1] = rt[256 + ((p1 >> rs) & rm)] + gt[256 + ((p1 >> gs) & gm)] + bt[256 + ((p1 >> bs) & bm)];
		d[x + 2] = rt[512 + ((p2 >> rs) & rm)] + gt[512 + ((p2 >> gs) & gm)] + bt[512 + ((p2 >> bs) & bm)];
		d[x + 3] = rt[768 + ((p3 >> rs) & rm)] + gt[768 + ((p3 >> gs) & gm)] + bt[768 + ((p3 >> bs) & bm)];
	}
	for (; x < x1; x++, s++) {
		const unsigned p = *s;
		const int c = (x & 3) * 256;
		d[x] = rt[c + ((p >> rs) & rm)] + gt[c + ((p >> gs) & gm)] + bt[c + ((p >> bs) & bm)];
	}
}

/** The same, written the plain way, one pixel at a time: the check for dosDitherRow. */
static inline void dosDitherRowReference(uint8_t *d, const uint16_t *s, int x0, int x1,
        const uint8_t *rt, const uint8_t *gt, const uint8_t *bt,
        int rs, int gs, int bs, unsigned rm, unsigned gm, unsigned bm) {
	for (int x = x0; x < x1; x++) {
		const unsigned p = s[x - x0];
		const int column = (x & 3) * 256;
		d[x] = (uint8_t)(rt[column + ((p >> rs) & rm)] + gt[column + ((p >> gs) & gm)] + bt[column + ((p >> bs) & bm)]);
	}
}

#endif
