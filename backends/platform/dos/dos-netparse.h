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

#ifndef BACKENDS_PLATFORM_DOS_NETPARSE_H
#define BACKENDS_PLATFORM_DOS_NETPARSE_H

#include "common/scummsys.h"
#include "common/endian.h"

// IPv4 address text for dos-net.cpp; no sockets, so unit tested on the host. Like SDL_net, an
// address is held in network byte order: its first number is its first byte in memory.

/**
 * Parse a strict "a.b.c.d" address.
 *
 * @param text  four decimal numbers from 0 to 255 separated by dots, and nothing else
 * @param host  set to the address in network byte order; left alone if the text is refused
 * @return whether the text was such an address
 */
inline bool dosParseDotted(const char *text, uint32 &host) {
	uint32 part[4];
	int n = 0;
	const char *p = text;
	while (n < 4) {
		if (*p < '0' || *p > '9')
			return false;
		uint32 value = 0;
		while (*p >= '0' && *p <= '9') {
			value = value * 10 + (*p - '0');
			if (value > 255)
				return false;
			p++;
		}
		part[n++] = value;
		if (n < 4) {
			if (*p != '.')
				return false;
			p++;
		}
	}
	if (*p != 0)
		return false;

	host = TO_BE_32((part[0] << 24) | (part[1] << 16) | (part[2] << 8) | part[3]);
	return true;
}

/**
 * Format "a.b.c.d" into text.
 *
 * @param text  the buffer, at least 16 bytes; text that does not fit is cut, and always ended
 * @param size  its size in bytes
 */
inline void dosFormatDotted(char *text, size_t size, uint a, uint b, uint c, uint d) {
	snprintf(text, size, "%u.%u.%u.%u", a, b, c, d);
}

/**
 * Format an address held in network byte order, as dosParseDotted() gives it.
 *
 * @param text  the buffer, at least 16 bytes
 * @param size  its size in bytes
 * @param host  the address in network byte order
 */
inline void dosFormatHost(char *text, size_t size, uint32 host) {
	const uint32 value = FROM_BE_32(host);
	dosFormatDotted(text, size, value >> 24, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
}

#endif
