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

// IPv4 address text and byte order for dos-net.cpp; no sockets, so unit tested on the host

#ifndef BACKENDS_PLATFORM_DOS_NETPARSE_H
#define BACKENDS_PLATFORM_DOS_NETPARSE_H

#include <stdint.h>
#include <stdio.h>

/** Parse a strict "a.b.c.d" into SDL_net's network byte order */
static inline bool dosParseDotted(const char *text, uint32_t &host) {
	unsigned part[4];
	int n = 0;
	const char *p = text;
	while (n < 4) {
		if (*p < '0' || *p > '9')
			return false;
		unsigned value = 0;
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

	// In network order: the first number is the first byte in memory
	host = part[0] | (part[1] << 8) | (part[2] << 16) | ((uint32_t)part[3] << 24);
	return true;
}

/** Format "a.b.c.d" into text (at least 16 bytes) */
static inline void dosFormatDotted(char *text, size_t size, unsigned a, unsigned b, unsigned c, unsigned d) {
	snprintf(text, size, "%u.%u.%u.%u", a, b, c, d);
}

/** Format an address held in network byte order */
static inline void dosFormatHost(char *text, size_t size, uint32_t host) {
	dosFormatDotted(text, size, host & 0xFF, (host >> 8) & 0xFF, (host >> 16) & 0xFF, (host >> 24) & 0xFF);
}

/** Swap the bytes of a port number to or from network order */
static inline uint16_t dosSwap16(uint16_t value) {
	return (uint16_t)((value >> 8) | (value << 8));
}

#endif
