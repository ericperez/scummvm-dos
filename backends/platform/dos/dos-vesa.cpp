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

// This file talks to the video BIOS and video memory directly
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <dpmi.h>
#include <go32.h>
#include <string.h>
#include <sys/movedata.h>

#include "backends/platform/dos/dos-vesa.h"

// Fields of the VBE "mode information block" that we use
struct VesaModeInfo {
	uint16_t attributes;      ///< bit 0 supported, bit 4 graphics, bit 7 linear framebuffer
	uint8_t windowAAttributes;
	uint16_t granularityKB;
	uint16_t windowSizeKB;
	uint16_t windowASegment;
	uint16_t pitch;
	uint16_t xResolution;
	uint16_t yResolution;
	uint8_t bitsPerPixel;
	uint8_t memoryModel;      ///< 4 = packed pixel (one byte per pixel at 8 bpp)
	uint32_t physBase;        ///< linear framebuffer address (VBE 2.0)
};

static uint16_t rd16(const uint8_t *p) {
	return p[0] | (p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

// VBE calls pass data in DOS memory: DJGPP's transfer buffer, at __tb
static void setTransferBuffer(__dpmi_regs &r) {
	r.x.es = __tb >> 4;
	r.x.di = __tb & 15;
}

static bool vbeOk(const __dpmi_regs &r) {
	return r.x.ax == 0x004F; // AL = function supported, AH = success
}

// How many screens of this size the card's memory holds, at most limit
static int pagesFor(int pitch, int height, int limit) {
	if (limit < 2)
		return 1;
	int kb = VesaDisplay::videoMemoryKB();
	unsigned long page = (unsigned long)pitch * height;
	if (kb <= 0 || page == 0)
		return 1;
	unsigned long n = (unsigned long)kb * 1024 / page;
	if (n < 2)
		return 1;
	return n > (unsigned long)limit ? limit : (int)n;
}

// VBE function 07h: the first scan line of the display. At the vertical
// retrace if asked to, which the BIOS waits for.
static bool setDisplayStart(int line, bool atRetrace) {
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F07;
	r.x.bx = atRetrace ? 0x0080 : 0x0000;
	r.x.cx = 0;
	r.x.dx = line;
	__dpmi_int(0x10, &r);
	return vbeOk(r);
}

static bool getModeInfo(uint16_t mode, uint8_t *block /* 256 bytes */) {
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F01;
	r.x.cx = mode;
	setTransferBuffer(r);
	__dpmi_int(0x10, &r);
	if (!vbeOk(r))
		return false;

	dosmemget(__tb, 256, block);
	return true;
}

static void parseModeInfo(const uint8_t *b, VesaModeInfo &mi);

VesaDisplay::VesaDisplay() :
	_active(false), _linear(false), _pageLimit(1), _pages(1), _drawPage(0), _showPage(0),
	_width(0), _height(0), _pitch(0),
	_selector(-1), _mapAddress(0), _mapSize(0), _mapped(false),
	_granularity(0), _windowSize(0), _windowSegment(0), _currentBank(0xFFFFFFFF) {
}

VesaDisplay::~VesaDisplay() {
	release();
}

static void parseModeInfo(const uint8_t *b, VesaModeInfo &mi) {
	mi.attributes = rd16(b + 0x00);
	mi.windowAAttributes = b[0x02];
	mi.granularityKB = rd16(b + 0x04);
	mi.windowSizeKB = rd16(b + 0x06);
	mi.windowASegment = rd16(b + 0x08);
	mi.pitch = rd16(b + 0x10);
	mi.xResolution = rd16(b + 0x12);
	mi.yResolution = rd16(b + 0x14);
	mi.bitsPerPixel = b[0x19];
	mi.memoryModel = b[0x1B];
	mi.physBase = rd32(b + 0x28);
}

// The mode numbers to try: the card's own list plus the standard ones.
// Returns 0 if the card has no VBE.
int VesaDisplay::readModeList(uint16_t *candidates, int maxCount, int *vbeVersion) {
	if (vbeVersion)
		*vbeVersion = 0;

	// Ask for the VBE 2.0 information block
	uint8_t block[512];
	memset(block, 0, sizeof(block));
	memcpy(block, "VBE2", 4);
	dosmemput(block, sizeof(block), __tb);

	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F00;
	setTransferBuffer(r);
	__dpmi_int(0x10, &r);
	if (!vbeOk(r))
		return 0;

	dosmemget(__tb, sizeof(block), block);
	if (memcmp(block, "VESA", 4) != 0)
		return 0;

	if (vbeVersion)
		*vbeVersion = rd16(block + 4);

	// The list of mode numbers is a far pointer into DOS memory, ended by 0xFFFF
	uint32_t modePtr = rd32(block + 14);
	uint32_t modeList = ((modePtr >> 16) << 4) + (modePtr & 0xFFFF);

	int count = 0;
	if (modeList) {
		for (; count < maxCount - 8; count++) {
			uint8_t w[2];
			dosmemget(modeList + count * 2, 2, w);
			uint16_t m = rd16(w);
			if (m == 0xFFFF)
				break;
			candidates[count] = m;
		}
	}

	// The mode numbers every VBE card is supposed to have, in case the
	// list is missing or unreadable
	static const uint16_t standardModes[] = { 0x100, 0x101, 0x103, 0x105, 0x107 };
	for (unsigned i = 0; i < sizeof(standardModes) / sizeof(standardModes[0]) && count < maxCount; i++)
		candidates[count++] = standardModes[i];

	return count;
}

// The video memory the card says it has, in KB; 0 if it does not say.
// The VBE information block holds it in 64 KB blocks at offset 18.
int VesaDisplay::videoMemoryKB() {
	uint8_t block[512];
	memset(block, 0, sizeof(block));
	memcpy(block, "VBE2", 4);
	dosmemput(block, sizeof(block), __tb);

	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F00;
	setTransferBuffer(r);
	__dpmi_int(0x10, &r);
	if (!vbeOk(r))
		return 0;

	dosmemget(__tb, sizeof(block), block);
	if (memcmp(block, "VESA", 4) != 0)
		return 0;
	return rd16(block + 18) * 64;
}

int VesaDisplay::listModes(DosModeSize *out, int maxCount, int *vbeVersion) {
	uint16_t candidates[260];
	int count = readModeList(candidates, 260, vbeVersion);

	int n = 0;
	for (int i = 0; i < count && n < maxCount; i++) {
		uint8_t mb[256];
		if (!getModeInfo(candidates[i], mb))
			continue;

		VesaModeInfo mi;
		parseModeInfo(mb, mi);

		if (!(mi.attributes & 0x01) || !(mi.attributes & 0x10))
			continue; // not supported, or text mode
		if (mi.bitsPerPixel != 8 || mi.memoryModel != 4)
			continue;

		bool seen = false;
		for (int j = 0; j < n; j++) {
			if (out[j].w == mi.xResolution && out[j].h == mi.yResolution)
				seen = true;
		}
		if (seen)
			continue;

		out[n].w = mi.xResolution;
		out[n].h = mi.yResolution;
		n++;
	}
	return n;
}

bool VesaDisplay::readEdid(uint8_t *block) {
	// VBE/DDC function 15h, subfunction 1: read the EDID of display 0
	uint8_t zero[128];
	memset(zero, 0, sizeof(zero));
	dosmemput(zero, sizeof(zero), __tb);

	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F15;
	r.x.bx = 0x0001;
	r.x.cx = 0; // controller 0
	r.x.dx = 0; // EDID block 0
	setTransferBuffer(r);
	__dpmi_int(0x10, &r);
	if (!vbeOk(r))
		return false;

	dosmemget(__tb, 128, block);
	return true;
}

bool VesaDisplay::findMode(int width, int height, bool wantLinear, uint16_t &mode, VesaModeInfo &info) {
	uint16_t candidates[260];
	int count = readModeList(candidates, 260, nullptr);
	if (!count)
		return false;

	for (int i = 0; i < count; i++) {
		uint8_t mb[256];
		if (!getModeInfo(candidates[i], mb))
			continue;

		VesaModeInfo mi;
		parseModeInfo(mb, mi);

		if (!(mi.attributes & 0x01) || !(mi.attributes & 0x10))
			continue; // not supported, or text mode
		if (mi.xResolution != width || mi.yResolution != height)
			continue;
		if (mi.bitsPerPixel != 8 || mi.memoryModel != 4)
			continue;
		if (wantLinear && (!(mi.attributes & 0x80) || mi.physBase == 0))
			continue;

		mode = candidates[i];
		info = mi;
		return true;
	}

	return false;
}

bool VesaDisplay::setMode(int width, int height, bool allowLinear) {
	release();

	uint16_t mode = 0;
	VesaModeInfo mi;
	bool linear = false;

	if (allowLinear && findMode(width, height, true, mode, mi)) {
		// Map the video memory into our address space before switching: a
		// card or DPMI host that cannot do it just takes the banked way
		__dpmi_meminfo mem;
		mem.address = mi.physBase;
		mem.size = ((unsigned long)mi.pitch * height * pagesFor(mi.pitch, height, _pageLimit) + 0xFFF) & ~0xFFFul;
		if (__dpmi_physical_address_mapping(&mem) == 0) {
			int sel = __dpmi_allocate_ldt_descriptors(1);
			if (sel >= 0 &&
			        __dpmi_set_segment_base_address(sel, mem.address) == 0 &&
			        __dpmi_set_segment_limit(sel, mem.size - 1) == 0) {
				_selector = sel;
				_mapAddress = mem.address;
				_mapSize = mem.size;
				_mapped = true;
				linear = true;
			} else {
				if (sel >= 0)
					__dpmi_free_ldt_descriptor(sel);
				__dpmi_free_physical_address_mapping(&mem);
			}
		}
	}

	if (!linear && !findMode(width, height, false, mode, mi))
		return false;

	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F02;
	r.x.bx = mode | (linear ? 0x4000 : 0); // bit 14: linear framebuffer
	__dpmi_int(0x10, &r);
	if (!vbeOk(r)) {
		release();
		return false;
	}

	_active = true;
	_linear = linear;
	_width = width;
	_height = height;
	_pitch = mi.pitch ? mi.pitch : width;

	_granularity = (mi.granularityKB ? mi.granularityKB : 64) * 1024u;
	_windowSize = (mi.windowSizeKB ? mi.windowSizeKB : 64) * 1024u;
	_windowSegment = mi.windowASegment ? mi.windowASegment : 0xA000;
	_currentBank = 0xFFFFFFFF;
	return true;
}

void VesaDisplay::release() {
	if (_selector >= 0) {
		__dpmi_free_ldt_descriptor(_selector);
		_selector = -1;
	}
	if (_mapped) {
		__dpmi_meminfo mem;
		mem.address = _mapAddress;
		mem.size = _mapSize;
		__dpmi_free_physical_address_mapping(&mem);
		_mapped = false;
	}
	_active = false;
	_linear = false;
	_pages = 1;
	_drawPage = 0;
	_showPage = 0;
}

int VesaDisplay::setupPages() {
	_pages = 1;
	_drawPage = 0;
	_showPage = 0;
	if (!_active || _pageLimit < 2)
		return 1;

	int n = pagesFor(_pitch, _height, _pageLimit);
	if (n < 2)
		return 1;

	// The most scan lines the mode can have (VBE function 06h, get)
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F06;
	r.x.bx = 0x0001;
	__dpmi_int(0x10, &r);
	if (!vbeOk(r))
		return 1;
	int maxLines = r.x.dx;
	if (maxLines && maxLines / _height < n)
		n = maxLines / _height;
	if (n < 2)
		return 1;

	// A linear mapping made for a single page cannot hold the others
	if (_linear && _mapSize < (unsigned long)_pitch * _height * n)
		return 1;

	// The card has to take the display start function
	if (!setDisplayStart(0, false))
		return 1;

	_pages = n;
	_showPage = 0;
	_drawPage = 1;
	return n;
}

bool VesaDisplay::flip() {
	if (_pages < 2)
		return false;

	if (!setDisplayStart(_drawPage * _height, true)) {
		// Back to the first page, as before
		setDisplayStart(0, false);
		_pages = 1;
		_drawPage = 0;
		_showPage = 0;
		return false;
	}

	_showPage = _drawPage;
	_drawPage = (_drawPage + 1) % _pages;
	return true;
}

void VesaDisplay::switchBank(unsigned bank) {
	if (bank == _currentBank)
		return;

	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F05; // window control
	r.x.bx = 0;      // window A, set position
	r.x.dx = bank;   // in units of the granularity
	__dpmi_int(0x10, &r);
	_currentBank = bank;
}

void VesaDisplay::putRect(const uint8_t *src, int srcPitch, int x, int y, int w, int h) {
	if (!_active || w <= 0 || h <= 0)
		return;

	y += _drawPage * _height; // the page being drawn, off the display when flipping

	if (_linear) {
		if (x == 0 && w == _pitch && srcPitch == w) {
			// Whole rows are contiguous on both sides: one copy
			movedata(_my_ds(), (unsigned)src, _selector, y * _pitch, (size_t)w * h);
			return;
		}

		for (int row = 0; row < h; row++) {
			movedata(_my_ds(), (unsigned)(src + (size_t)row * srcPitch),
			         _selector, (y + row) * _pitch + x, w);
		}
		return;
	}

	// Banked: each row may cross from one bank to the next
	for (int row = 0; row < h; row++) {
		const uint8_t *s = src + (size_t)row * srcPitch;
		unsigned long offset = (unsigned long)(y + row) * _pitch + x;
		int left = w;

		while (left > 0) {
			unsigned bank = offset / _granularity;
			unsigned inWindow = offset - (unsigned long)bank * _granularity;
			unsigned room = _windowSize - inWindow;
			unsigned n = (unsigned)left < room ? (unsigned)left : room;

			switchBank(bank);
			dosmemput(s, n, (_windowSegment << 4) + inWindow);

			s += n;
			offset += n;
			left -= n;
		}
	}
}

void VesaDisplay::readAll(uint8_t *dst) {
	if (!_active)
		return;

	for (int row = 0; row < _height; row++) {
		uint8_t *d = dst + (size_t)row * _width;
		const int line = row + _showPage * _height;

		if (_linear) {
			movedata(_selector, line * _pitch, _my_ds(), (unsigned)d, _width);
			continue;
		}

		unsigned long offset = (unsigned long)line * _pitch;
		int left = _width;
		while (left > 0) {
			unsigned bank = offset / _granularity;
			unsigned inWindow = offset - (unsigned long)bank * _granularity;
			unsigned room = _windowSize - inWindow;
			unsigned n = (unsigned)left < room ? (unsigned)left : room;

			switchBank(bank);
			dosmemget((_windowSegment << 4) + inWindow, n, d);

			d += n;
			offset += n;
			left -= n;
		}
	}
}

#ifdef USE_RGB_COLOR

// The colour fields of a high colour mode, from its mode information block
static bool directFields(const uint8_t *b, uint8_t bits,
                         uint8_t &rs, uint8_t &rp, uint8_t &gs, uint8_t &gp, uint8_t &bs, uint8_t &bp) {
	rs = b[0x1F];
	rp = b[0x20];
	gs = b[0x21];
	gp = b[0x22];
	bs = b[0x23];
	bp = b[0x24];

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

int VesaDisplay::listDirectModes(VesaDirectMode *out, int maxCount) {
	uint16_t candidates[260];
	int count = readModeList(candidates, 260, nullptr);

	int n = 0;
	for (int i = 0; i < count && n < maxCount; i++) {
		uint8_t mb[256];
		if (!getModeInfo(candidates[i], mb))
			continue;

		VesaModeInfo mi;
		parseModeInfo(mb, mi);

		if (!(mi.attributes & 0x01) || !(mi.attributes & 0x10))
			continue; // not supported, or text mode
		if (mi.memoryModel != 6 || (mi.bitsPerPixel != 15 && mi.bitsPerPixel != 16))
			continue; // not direct colour at two bytes a pixel

		uint8_t rs, rp, gs, gp, bs, bp;
		if (!directFields(mb, mi.bitsPerPixel, rs, rp, gs, gp, bs, bp))
			continue;

		bool seen = false;
		for (int j = 0; j < n; j++) {
			if (out[j].width == mi.xResolution && out[j].height == mi.yResolution &&
			        out[j].redPos == rp && out[j].greenSize == gs)
				seen = true;
		}
		if (seen)
			continue;

		VesaDirectMode &m = out[n++];
		m.width = mi.xResolution;
		m.height = mi.yResolution;
		m.bpp = mi.bitsPerPixel;
		m.redSize = rs;
		m.redPos = rp;
		m.greenSize = gs;
		m.greenPos = gp;
		m.blueSize = bs;
		m.bluePos = bp;
		m.linear = (mi.attributes & 0x80) && mi.physBase != 0;
	}
	return n;
}

bool VesaDisplay::setDirectMode(int width, int height, int bpp, int redPos, bool allowLinear) {
	release();

	uint16_t candidates[260];
	int count = readModeList(candidates, 260, nullptr);

	uint16_t mode = 0;
	VesaModeInfo mi;
	bool found = false, linear = false;

	for (int pass = 0; pass < 2 && !found; pass++) {
		bool wantLinear = pass == 0;
		if (wantLinear && !allowLinear)
			continue;

		for (int i = 0; i < count && !found; i++) {
			uint8_t mb[256];
			if (!getModeInfo(candidates[i], mb))
				continue;

			VesaModeInfo m;
			parseModeInfo(mb, m);
			if (!(m.attributes & 0x01) || !(m.attributes & 0x10))
				continue;
			if (m.memoryModel != 6 || m.bitsPerPixel != bpp)
				continue;
			if (m.xResolution != width || m.yResolution != height)
				continue;

			uint8_t rs, rp, gs, gp, bs, bp;
			if (!directFields(mb, m.bitsPerPixel, rs, rp, gs, gp, bs, bp) || rp != redPos)
				continue;
			if (wantLinear && (!(m.attributes & 0x80) || m.physBase == 0))
				continue;

			mode = candidates[i];
			mi = m;
			found = true;
			linear = wantLinear;
		}
	}
	if (!found)
		return false;

	if (linear) {
		// Map the video memory before switching, as setMode() does; a card or
		// DPMI host that cannot do it takes the banked way
		__dpmi_meminfo mem;
		mem.address = mi.physBase;
		mem.size = ((unsigned long)mi.pitch * height * pagesFor(mi.pitch, height, _pageLimit) + 0xFFF) & ~0xFFFul;
		linear = false;
		if (__dpmi_physical_address_mapping(&mem) == 0) {
			int sel = __dpmi_allocate_ldt_descriptors(1);
			if (sel >= 0 &&
			        __dpmi_set_segment_base_address(sel, mem.address) == 0 &&
			        __dpmi_set_segment_limit(sel, mem.size - 1) == 0) {
				_selector = sel;
				_mapAddress = mem.address;
				_mapSize = mem.size;
				_mapped = true;
				linear = true;
			} else {
				if (sel >= 0)
					__dpmi_free_ldt_descriptor(sel);
				__dpmi_free_physical_address_mapping(&mem);
			}
		}

		if (!linear) {
			// Go round again for a mode with banked access
			if (!allowLinear)
				return false;
			return setDirectMode(width, height, bpp, redPos, false);
		}
	}

	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x4F02;
	r.x.bx = mode | (linear ? 0x4000 : 0);
	__dpmi_int(0x10, &r);
	if (!vbeOk(r)) {
		release();
		return false;
	}

	_active = true;
	_linear = linear;
	_width = width;
	_height = height;
	_pitch = mi.pitch ? mi.pitch : width * 2; // bytes

	_granularity = (mi.granularityKB ? mi.granularityKB : 64) * 1024u;
	_windowSize = (mi.windowSizeKB ? mi.windowSizeKB : 64) * 1024u;
	_windowSegment = mi.windowASegment ? mi.windowASegment : 0xA000;
	_currentBank = 0xFFFFFFFF;
	return true;
}

void VesaDisplay::readAll16(uint8_t *dst) {
	if (!_active)
		return;

	const int rowBytes = _width * 2;
	for (int row = 0; row < _height; row++) {
		uint8_t *d = dst + (size_t)row * rowBytes;
		const int line = row + _showPage * _height;

		if (_linear) {
			movedata(_selector, line * _pitch, _my_ds(), (unsigned)d, rowBytes);
			continue;
		}

		unsigned long offset = (unsigned long)line * _pitch;
		int left = rowBytes;
		while (left > 0) {
			unsigned bank = offset / _granularity;
			unsigned inWindow = offset - (unsigned long)bank * _granularity;
			unsigned room = _windowSize - inWindow;
			unsigned n = (unsigned)left < room ? (unsigned)left : room;

			switchBank(bank);
			dosmemget((_windowSegment << 4) + inWindow, n, d);

			d += n;
			offset += n;
			left -= n;
		}
	}
}

#endif // USE_RGB_COLOR
