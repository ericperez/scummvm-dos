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


#ifndef BACKENDS_PLATFORM_DOS_VESAINFO_H
#define BACKENDS_PLATFORM_DOS_VESAINFO_H

#include "common/endian.h"
#include "common/scummsys.h"

// The VESA BIOS Extension's information blocks, as dos-vesa.cpp reads them, and the arithmetic of
// display pages and memory banks. No DOS calls, so unit tested on the host.

/** VBE mode attributes (offset 0 of the mode information block) */
enum {
	kDosVesaModeSupported = 0x01,   ///< the mode works with the monitor and card
	kDosVesaModeGraphics  = 0x10,   ///< not a text mode
	kDosVesaModeLinear    = 0x80    ///< a linear framebuffer can be used (VBE 2.0)
};

/** VBE memory models (offset 0x1B of the mode information block) */
enum {
	kDosVesaPackedPixel = 4,        ///< one byte a pixel at 8 bits per pixel
	kDosVesaDirectColour = 6        ///< the colour fields described at offset 0x1F
};

/** The fields of the VBE "mode information block" that the port uses. */
struct VesaModeInfo {
	uint16 attributes;          ///< kDosVesaMode* flags
	byte windowAAttributes;
	uint16 granularityKB;       ///< the steps a bank can start at
	uint16 windowSizeKB;        ///< the size of the window the bank is seen through
	uint16 windowASegment;
	uint16 pitch;               ///< bytes a scan line
	uint16 xResolution;
	uint16 yResolution;
	byte bitsPerPixel;
	byte memoryModel;           ///< kDosVesaPackedPixel, kDosVesaDirectColour, ...
	uint32 physBase;            ///< linear framebuffer address (VBE 2.0)
};

/**
 * Read the fields the port uses from a mode information block (VBE function 01h).
 *
 * @param block  the 256 bytes the BIOS filled in (little endian)
 * @param mi     filled in
 */
inline void dosVesaParseModeInfo(const byte *block, VesaModeInfo &mi) {
	mi.attributes = READ_LE_UINT16(block + 0x00);
	mi.windowAAttributes = block[0x02];
	mi.granularityKB = READ_LE_UINT16(block + 0x04);
	mi.windowSizeKB = READ_LE_UINT16(block + 0x06);
	mi.windowASegment = READ_LE_UINT16(block + 0x08);
	mi.pitch = READ_LE_UINT16(block + 0x10);
	mi.xResolution = READ_LE_UINT16(block + 0x12);
	mi.yResolution = READ_LE_UINT16(block + 0x14);
	mi.bitsPerPixel = block[0x19];
	mi.memoryModel = block[0x1B];
	mi.physBase = READ_LE_UINT32(block + 0x28);
}

/** Is this a graphics mode the card and monitor can show? */
inline bool dosVesaIsUsableGraphicsMode(const VesaModeInfo &mi) {
	return (mi.attributes & kDosVesaModeSupported) && (mi.attributes & kDosVesaModeGraphics);
}

/** Is this a usable mode of 256 colours, one byte a pixel? */
inline bool dosVesaIs256ColourMode(const VesaModeInfo &mi) {
	return dosVesaIsUsableGraphicsMode(mi) && mi.bitsPerPixel == 8 && mi.memoryModel == kDosVesaPackedPixel;
}

/** Is this a usable "high colour" mode of two bytes a pixel? */
inline bool dosVesaIsHighColourMode(const VesaModeInfo &mi) {
	return dosVesaIsUsableGraphicsMode(mi) && mi.memoryModel == kDosVesaDirectColour &&
	       (mi.bitsPerPixel == 15 || mi.bitsPerPixel == 16);
}

/** Does the mode have a linear framebuffer the port can map? */
inline bool dosVesaHasLinearFramebuffer(const VesaModeInfo &mi) {
	return (mi.attributes & kDosVesaModeLinear) && mi.physBase != 0;
}

/**
 * The colour fields of a high colour mode, from its mode information block. A BIOS that
 * does not describe them gets the usual layouts: 5-6-5 at 16 bits, 5-5-5 at 15.
 *
 * @param block  the mode information block
 * @param bits   the mode's bits per pixel
 * @param rs, rp, gs, gp, bs, bp  set to the size and position of the red, green and blue fields
 * @return whether each field has 5 to 8 bits, which the port can convert to
 */
inline bool dosVesaDirectFields(const byte *block, byte bits, byte &rs, byte &rp, byte &gs, byte &gp, byte &bs, byte &bp) {
	rs = block[0x1F];
	rp = block[0x20];
	gs = block[0x21];
	gp = block[0x22];
	bs = block[0x23];
	bp = block[0x24];

	if (!rs && !gs && !bs) {
		// A BIOS that does not describe the fields: the usual layouts
		if (bits == 16) {
			rs = 5;
			rp = 11;
			gs = 6;
			gp = 5;
			bs = 5;
			bp = 0;
		} else {
			rs = 5;
			rp = 10;
			gs = 5;
			gp = 5;
			bs = 5;
			bp = 0;
		}
	}
	return rs >= 5 && gs >= 5 && bs >= 5 && rs <= 8 && gs <= 8 && bs <= 8;
}

/**
 * The linear address of a real mode far pointer (segment:offset), as the VBE information
 * block gives the list of mode numbers.
 */
inline uint32 dosVesaRealPointer(uint32 farPointer) {
	return ((farPointer >> 16) << 4) + (farPointer & 0xFFFF);
}

/**
 * The video memory of the card, from the VBE information block (VBE function 00h).
 *
 * @param block  the information block, after its "VESA" signature was checked
 * @return the memory in KB
 */
inline int dosVesaMemoryKB(const byte *block) {
	return READ_LE_UINT16(block + 18) * 64;
}

/**
 * How many screens of a size the card's memory holds, for page flipping.
 *
 * @param memoryKB  the card's video memory; 0 if unknown
 * @param pitch     bytes a scan line
 * @param height    scan lines a screen
 * @param limit     the most pages wanted
 * @return 1 to limit; 1 when fewer than two fit, or when the memory is unknown
 */
inline int dosVesaPages(int memoryKB, int pitch, int height, int limit) {
	if (limit < 2)
		return 1;
	const unsigned long page = (unsigned long)pitch * height;
	if (memoryKB <= 0 || page == 0)
		return 1;
	const unsigned long n = (unsigned long)memoryKB * 1024 / page;
	if (n < 2)
		return 1;
	return n > (unsigned long)limit ? limit : (int)n;
}

/**
 * Where a run of bytes of video memory starts in a banked mode, and how much of it the
 * window shows from there.
 *
 * @param offset       where the run starts in video memory
 * @param left         its length
 * @param granularity  the steps a bank can start at, in bytes
 * @param windowSize   the size of the window, in bytes
 * @param bank         set to the bank to switch to (in steps of the granularity)
 * @param inWindow     set to where the run starts in the window
 * @return the bytes that can be copied before the next bank is needed, at most left
 */
inline uint dosVesaBankSpan(unsigned long offset, uint left, uint granularity, uint windowSize, uint &bank, uint &inWindow) {
	bank = offset / granularity;
	inWindow = offset - (unsigned long)bank * granularity;
	const uint room = windowSize - inWindow;
	return left < room ? left : room;
}

#endif
