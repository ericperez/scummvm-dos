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

#ifndef BACKENDS_PLATFORM_DOS_VESA_H
#define BACKENDS_PLATFORM_DOS_VESA_H

#include <stdint.h>

// USE_RGB_COLOR changes what this file declares
#if defined(HAVE_CONFIG_H)
	#include "config.h"
#endif

#include "backends/platform/dos/dos-modes.h"

/**
 * VESA graphics modes above 320x200, through a linear framebuffer when the card
 * has one, and through 64KB banks when it does not.
 */
struct VesaModeInfo;

#ifdef USE_RGB_COLOR
/** A 15 or 16 bit "high colour" mode, with the card's layout of its colour fields */
struct VesaDirectMode {
	uint16_t width, height;
	uint8_t bpp;                       ///< 15 or 16
	uint8_t redSize, redPos;
	uint8_t greenSize, greenPos;
	uint8_t blueSize, bluePos;
	bool linear;                       ///< the card offers a linear framebuffer for it
};
#endif

class VesaDisplay {
public:
	VesaDisplay();
	~VesaDisplay();

	/**
	 * Switch to an 8-bit mode of exactly this size.
	 *
	 * @param allowLinear Set to false to force the banked way (for testing).
	 * @return true if the mode was found and set.
	 */
	bool setMode(int width, int height, bool allowLinear = true);

	/** Drop the video memory mapping. Does not change the video mode. */
	void release();

	/**
	 * The sizes of the card's 256 colour modes, each once.
	 *
	 * @param vbeVersion If not null, set to the VBE version (0 without VBE).
	 * @return How many sizes were stored in out.
	 */
	static int listModes(DosModeSize *out, int maxCount, int *vbeVersion = nullptr);

	/** The video memory the card reports, in KB; 0 if there is no VBE or it does not say. */
	static int videoMemoryKB();

	/**
	 * Ask the monitor for its EDID block through VBE/DDC.
	 *
	 * @param block 128 bytes.
	 * @return false if the card or the monitor does not answer.
	 */
	static bool readEdid(uint8_t *block);

	/** The most display pages to use (1: no page flipping). Call before setting a mode. */
	void setPageLimit(int pages) {
		_pageLimit = pages;
	}

	/** After a mode is set: how many pages the card can flip between (1 if it cannot). */
	int setupPages();

	int pages() const {
		return _pages;
	}

	/**
	 * Show the drawn page at the next vertical retrace, and draw to the other one.
	 * @return false if the card refused: it is back to one page, to be redrawn whole.
	 */
	bool flip();

	bool isActive() const {
		return _active;
	}
	bool isLinear() const {
		return _linear;
	}
	int getWidth() const {
		return _width;
	}
	int getHeight() const {
		return _height;
	}

	/**
	 * Copy a rectangle of pixels to the screen (in high colour, x and w are in bytes).
	 *
	 * @param src Top left pixel of the rectangle in main memory.
	 * @param srcPitch Distance in bytes from one row of src to the next.
	 */
	void putRect(const uint8_t *src, int srcPitch, int x, int y, int w, int h);

	/** Read the page on the display back into dst (width * height bytes). For testing. */
	void readAll(uint8_t *dst);

	#ifdef USE_RGB_COLOR
	/**
	 * The 15 and 16 bit modes the card offers (each size and colour layout once).
	 * @return How many were stored in out.
	 */
	static int listDirectModes(VesaDirectMode *out, int maxCount);

	/**
	 * Switch to a high colour mode: the size, the bits per pixel and the position
	 * of the red field pick it (565 has red at bit 11, 555 at bit 10).
	 */
	bool setDirectMode(int width, int height, int bpp, int redPos, bool allowLinear = true);

	/** Read the screen back into dst (width * height 16-bit pixels), for testing. */
	void readAll16(uint8_t *dst);
	#endif

private:
	static int readModeList(uint16_t *candidates, int maxCount, int *vbeVersion);
	bool findMode(int width, int height, bool wantLinear, uint16_t &mode, VesaModeInfo &info);
	void switchBank(unsigned bank);

	bool _active;
	bool _linear;
	int _pageLimit;             ///< Most pages to use
	int _pages;                 ///< Pages in use: 1, or 2 when flipping
	int _drawPage;              ///< The page putRect() writes to
	int _showPage;              ///< The page on the display
	int _width, _height;
	int _pitch;                 ///< Bytes per scan line in video memory

	// Linear framebuffer

	int _selector;              ///< LDT selector whose base is the framebuffer
	unsigned long _mapAddress;  ///< Linear address of the physical mapping
	unsigned long _mapSize;
	bool _mapped;

	// Banked access

	unsigned _granularity;      ///< Bytes between bank positions
	unsigned _windowSize;       ///< Bytes visible through the window
	unsigned _windowSegment;    ///< Real mode segment of the window (usually 0xA000)
	unsigned _currentBank;
};

#endif
