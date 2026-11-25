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

#ifndef BACKENDS_PLATFORM_DOS_BANNER_H
#define BACKENDS_PLATFORM_DOS_BANNER_H

#include <stddef.h>
#include <string.h>

// The banner shown until a game draws, in the 8x8 BIOS font; no DOS calls, so unit tested
// on the host

struct DosBannerLayout {
	int scale;    ///< screen pixels for one pixel of the 8x8 font
	int margin;   ///< from the top left corner of the screen
	int glyph;    ///< width and height of a character
	int lineStep; ///< from the top of one line to the top of the next
	int perLine;  ///< characters that fit across
	int maxLines; ///< lines that fit down
};

/** The layout for a screen this size: text as large as on 320x200, where the font is drawn 1:1 */
static inline DosBannerLayout dosBannerLayout(int width, int height) {
	const int byWidth = width / 320, byHeight = height / 200;
	int scale = byWidth < byHeight ? byWidth : byHeight;
	if (scale < 1)
		scale = 1;

	DosBannerLayout l;
	l.scale = scale;
	l.margin = 4 * scale;
	l.glyph = 8 * scale;
	l.lineStep = l.glyph + scale;
	l.perLine = (width - 2 * l.margin) / l.glyph;
	if (l.perLine < 8)
		l.perLine = 8;
	l.maxLines = (height - 2 * l.margin) / l.lineStep;
	if (l.maxLines < 1)
		l.maxLines = 1;
	return l;
}

/**
 * Word-wrap text into out: at most maxLines lines of perLine characters, each ending in a
 * newline (long words are cut, empty lines dropped). Returns the number of lines.
 */
static inline int dosBannerWrap(const char *text, int perLine, int maxLines, char *out, size_t outSize) {
	size_t used = 0;
	int lines = 0;
	const char *p = text;
	if (outSize)
		out[0] = '\0';
	if (perLine < 1 || !outSize)
		return 0;

	while (*p && lines < maxLines) {
		// A line does not start with the space it was broken at
		while (*p == ' ')
			p++;
		if (*p == '\n') {
			p++;
			continue;
		}
		if (!*p)
			break;

		int n = 0, lastSpace = -1;
		while (p[n] && p[n] != '\n' && n < perLine) {
			if (p[n] == ' ')
				lastSpace = n;
			n++;
		}
		int take = n;
		if (p[n] && p[n] != '\n' && p[n] != ' ' && lastSpace > 0)
			take = lastSpace;
		int keep = take;
		while (keep > 0 && p[keep - 1] == ' ')
			keep--;

		if (used + keep + 2 > outSize)
			break;
		memcpy(out + used, p, keep);
		used += keep;
		out[used++] = '\n';
		out[used] = '\0';
		lines++;

		p += take;
		if (*p == '\n')
			p++;
	}
	return lines;
}

#endif
