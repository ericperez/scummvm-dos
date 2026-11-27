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


#ifndef BACKENDS_PLATFORM_DOS_BLASTER_H
#define BACKENDS_PLATFORM_DOS_BLASTER_H

#include "common/scummsys.h"

// The settings of a Sound Blaster card, from the BLASTER variable, and the arithmetic of its
// DSP's sample rates, for dos-sb.cpp, dos-midi.cpp and dos-cdaudio.cpp. No DOS calls, so unit
// tested on the host.

/** What a BLASTER variable such as "A220 I5 D1 H5 P330 T6" says about the card. */
struct DosBlaster {
	uint base;   ///< A: the card's base port (hexadecimal), 0 if not given
	int irq;     ///< I: its IRQ, -1 if not given
	int dma;     ///< D: its 8-bit DMA channel, -1 if not given
	int hdma;    ///< H: its 16-bit DMA channel (SB16), -1 if not given
	uint mpu;    ///< P: the port of its MPU-401 MIDI interface (hexadecimal), 0 if not given
	uint type;   ///< T: the card's type (1, 3: SB 1.x, 2.0; 2, 4, 5: SB Pro; 6: SB16), 0 if not given
};

/**
 * Read a number from a BLASTER field's value.
 *
 * @param p      the first character of the value; left on the first character after it
 * @param radix  10 or 16 (either case)
 * @param any    set to whether there was at least one digit
 * @return the value, 0 if there was no digit
 */
inline uint dosBlasterNumber(const char *&p, uint radix, bool &any) {
	uint value = 0;
	any = false;
	for (;; p++) {
		const char c = (*p >= 'A' && *p <= 'Z') ? (char)(*p - 'A' + 'a') : *p;
		uint digit;
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (radix == 16 && c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else
			return value;
		value = value * radix + digit;
		any = true;
	}
}

/**
 * Read a BLASTER variable. Its fields are a letter (either case) and a value, separated by
 * spaces; unknown fields, and fields whose value has no digit, are passed over.
 *
 * @param text  the variable's value; nullptr reads as an empty one
 * @return the settings, those not given left at their defaults (see DosBlaster)
 */
inline DosBlaster dosParseBlaster(const char *text) {
	DosBlaster b;
	b.base = 0;
	b.irq = -1;
	b.dma = -1;
	b.hdma = -1;
	b.mpu = 0;
	b.type = 0;

	const char *p = text;
	while (p && *p) {
		if (*p == ' ') {
			p++;
			continue;
		}

		const char tag = (*p >= 'a' && *p <= 'z') ? (char)(*p - 'a' + 'A') : *p;
		p++;
		bool any;
		uint value;
		switch (tag) {
		case 'A':
			value = dosBlasterNumber(p, 16, any);
			if (any)
				b.base = value;
			break;
		case 'I':
			value = dosBlasterNumber(p, 10, any);
			if (any)
				b.irq = (int)value;
			break;
		case 'D':
			value = dosBlasterNumber(p, 10, any);
			if (any)
				b.dma = (int)value;
			break;
		case 'H':
			value = dosBlasterNumber(p, 10, any);
			if (any)
				b.hdma = (int)value;
			break;
		case 'P':
			value = dosBlasterNumber(p, 16, any);
			if (any)
				b.mpu = value;
			break;
		case 'T':
			value = dosBlasterNumber(p, 10, any);
			if (any)
				b.type = value;
			break;
		default:
			break;
		}

		// Whatever is left of the field
		while (*p && *p != ' ')
			p++;
	}
	return b;
}

/**
 * Read a hexadecimal port number, such as "330", up to the first character that is no digit.
 *
 * @return the number, or 0 if the text does not start with a hexadecimal digit
 */
inline uint dosParseHex(const char *text) {
	if (!text)
		return 0;
	bool any;
	return dosBlasterNumber(text, 16, any);
}

/**
 * The DSP time constant of 8-bit playback (DSP command 40h) for a sample rate. The DSP plays
 * at 1000000 / (256 - time constant) Hz, from 4000 Hz up to 22222 Hz on an SB 2.0.
 *
 * @param requestedRate  the rate asked for, in Hz
 * @param rate           set to the rate the DSP will really play at, in Hz
 * @return the time constant
 */
inline byte dosSbTimeConstant(uint requestedRate, uint &rate) {
	if (requestedRate < 4000)
		requestedRate = 4000;
	uint divisor = (1000000 + requestedRate / 2) / requestedRate;
	if (divisor < 45)
		divisor = 45;
	if (divisor > 250)
		divisor = 250;
	rate = 1000000 / divisor;
	return (byte)(256 - divisor);
}

/**
 * The sample rate of 16-bit playback on an SB16 (DSP command 41h), which takes the rate
 * itself, from 5000 Hz to 44100 Hz.
 *
 * @param requestedRate  the rate asked for, in Hz
 * @return the rate the DSP is given, in Hz
 */
inline uint dosSb16Rate(uint requestedRate) {
	if (requestedRate < 5000)
		return 5000;
	if (requestedRate > 44100)
		return 44100;
	return requestedRate;
}

#endif
