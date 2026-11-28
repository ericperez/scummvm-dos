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

// This file talks directly to VGA hardware, so allow every system symbol.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <dpmi.h>
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>
#include <time.h>

#include "backends/platform/dos/dos-graphics.h"
#include "backends/platform/dos/dos-input.h"

#include "common/config-manager.h"
#include "common/endian.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "common/util.h"
#include "engines/engine.h"

static const uint32 kVideoMemory = 0xA0000;

static DOSGraphicsManager *g_instance = nullptr;

// Called by exit(), so that an error() in the middle of a game does not
// leave the machine in a graphics mode.
static void restoreTextModeAtExit() {
	if (g_instance)
		g_instance->restoreTextMode();
}

DOSGraphicsManager::DOSGraphicsManager() :
	_modeSet(false),
	_screenChangeID(0),
	_physW(kVgaWidth), _physH(kVgaHeight), _useVesa(false),
	_offX(0), _offY(0),
	_pending(false), _pendingW(0), _pendingH(0), _pendingFormatOk(true),
	_cursorPaletteEnabled(false),
	_overlayVisible(false), _overlayRaised(false),
	_cursor(nullptr), _cursorW(0), _cursorH(0), _hotX(0), _hotY(0),
	_keycolor(0), _cursorVisible(false), _cursorMapDirty(true),
	_hostMouseX(kVgaWidth / 2), _hostMouseY(kVgaHeight / 2),
	_dirtyCount(0), _dirtyAll(true), _cursorDirty(true),
	_verify(false), _verifyBuf(nullptr), _vramBuf(nullptr), _verifyFrames(0), _verifyBad(0),
	_shotInterval(0), _shotNext(0), _shotCount(0) {
	memset(_palette, 0, sizeof(_palette));
	_palettePending = false;
	_dacKnown = false;
	_bigBlitTicks = 0;
	_flipping = false;
	_romFontLoaded = false;
	_lastFlip = 0;
	memset(_cursorPalette, 0, sizeof(_cursorPalette));
	for (int i = 0; i < 256; i++)
		_cursorMap[i] = i;

	// Big enough for a game's screen; grows if the menus get a bigger mode
	_compositeSize = kMaxWidth * kMaxHeight;
	_composite = new byte[_compositeSize];
	memset(_composite, 0, _compositeSize);

	_bannerLayout = dosBannerLayout(kVgaWidth, kVgaHeight);
	_bannerBlank = false;
	_guiModeChosen = false;
	_guiModeValid = false;
	_guiW = _guiH = 0;
	_fittingNoted = false;

	#ifdef USE_RGB_COLOR
	_physBpp = 1;
	_physFormat = Graphics::PixelFormat::createFormatCLUT8();
	_scale = 1;
	_directCount = 0;
	_directListed = false;
	_pendingFormat = Graphics::PixelFormat::createFormatCLUT8();
	_lutGame = nullptr;
	_lutOverlay = nullptr;
	memset(_cursorMap16, 0, sizeof(_cursorMap16));
	_cursorBpp = 1;
	_cursorFormat = Graphics::PixelFormat::createFormatCLUT8();
	_guiBpp = 8;
	_guiRedPos = 11;
	_dither = false;
	memset(_ditherR, 0, sizeof(_ditherR));
	memset(_ditherG, 0, sizeof(_ditherG));
	memset(_ditherB, 0, sizeof(_ditherB));
	memset(_plainR, 0, sizeof(_plainR));
	memset(_plainG, 0, sizeof(_plainG));
	memset(_plainB, 0, sizeof(_plainB));
	#endif

	_overlay.create(_physW, _physH, getOverlayFormat());
	memset(_overlay.getPixels(), 0, _overlay.pitch * _overlay.h);

	const char *shots = getenv("SCUMMVM_DOS_SHOTS");
	if (shots && atoi(shots) > 0) {
		_shotInterval = (uint64)atoi(shots) * UCLOCKS_PER_SEC;
		_shotNext = (uint64)uclock() + _shotInterval;
	}

	_verify = getenv("SCUMMVM_DOS_VERIFY") != nullptr;
	if (_verify) {
		#ifdef USE_RGB_COLOR
		// room for two bytes a pixel
		_verifyBuf = new byte[kGuiMaxWidth * kGuiMaxHeight * 2];
		_vramBuf = new byte[kGuiMaxWidth * kGuiMaxHeight * 2];
		#else
		_verifyBuf = new byte[kGuiMaxWidth * kGuiMaxHeight];
		_vramBuf = new byte[kGuiMaxWidth * kGuiMaxHeight];
		#endif
	}

	g_instance = this;
	atexit(restoreTextModeAtExit);
}

DOSGraphicsManager::~DOSGraphicsManager() {
	restoreTextMode();
	if (g_instance == this)
		g_instance = nullptr;

	_screen.free();
	_overlay.free();
	delete[] _cursor;
	delete[] _composite;
	delete[] _verifyBuf;
	delete[] _vramBuf;
	#ifdef USE_RGB_COLOR
	delete[] _lutGame;
	delete[] _lutOverlay;
	#endif
}

// Switch the physical screen: VGA mode 13h for 320x200, a VESA mode for
// anything bigger.
bool DOSGraphicsManager::setPhysicalMode(int width, int height) {
	_banner.clear();
	_bannerBlank = false;
	#ifdef USE_RGB_COLOR
	// A high colour screen of the same size is a different mode
	if (_modeSet && _physBpp == 1 && _physW == width && _physH == height)
		return true;
	#else
	if (_modeSet && _physW == width && _physH == height)
		return true;
	#endif

	_flipping = false;
	if (width == kVgaWidth && height == kVgaHeight) {
		_vesa.release();
		_useVesa = false;

		__dpmi_regs r;
		r.x.ax = 0x0013; // BIOS: set video mode 13h, 320x200 256 colours
		__dpmi_int(0x10, &r);
	} else {
		// SCUMMVM_DOS_BANKED=1 forces the slower banked access, for a card
		// whose linear framebuffer does not work (and for testing)
		bool allowLinear = getenv("SCUMMVM_DOS_BANKED") == nullptr;
		_vesa.setPageLimit(pageFlipWanted() ? 2 : 1);
		if (!_vesa.setMode(width, height, allowLinear)) {
			warning("DOSGFX: no 256 colour VESA mode of %dx%d", width, height);
			return false;
		}
		_useVesa = true;
		// In a graphics mode first, so that its message goes to the log only
		_modeSet = true;
		setupPageFlipping();
	}

	_modeSet = true;
	logModeNotes();
	_physW = width;
	_physH = height;
	#ifdef USE_RGB_COLOR
	_physBpp = 1;
	_scale = 1;
	#endif

	if (_useVesa)
		warning("DOSGFX: VESA mode %dx%d, %s", width, height,
		        _vesa.isLinear() ? "linear framebuffer" : "banked access");
	else
		warning("DOSGFX: VGA mode 13h, %dx%d", width, height);

	// The frame that is sent to video memory has to hold the whole screen
	if ((size_t)_physW * _physH > _compositeSize) {
		delete[] _composite;
		_compositeSize = (size_t)_physW * _physH;
		_composite = new byte[_compositeSize];
	}

	// The overlay is as big as the screen, whatever it showed is gone
	_overlay.free();
	_overlay.create(_physW, _physH, getOverlayFormat());
	memset(_overlay.getPixels(), 0, _overlay.pitch * _overlay.h);

	// Reset the mouse driver only now that the mode is set: it adapts its
	// coordinate range to the mode.
	_hostMouseX = _physW / 2;
	_hostMouseY = _physH / 2;
	dosMouseInit(_physW, _physH);

	// A new mode starts with the BIOS default palette
	_dacKnown = false;
	_bigBlitTicks = 0; // the new mode may write at another speed
	if (_overlayVisible)
		writeOverlayDAC();
	else
		writeDAC(_palette, 0, 256);

	memset(_composite, 0, _physW * _physH);
	_lastCursorRect = Common::Rect();
	markAllDirty();
	_cursorDirty = true;
	return true;
}

// If no mode is set yet, set the GUI's (SCUMMVM_DOS_MODE=640 starts in 640x480, for testing)
void DOSGraphicsManager::ensureVideoMode() {
	if (_modeSet)
		return;

	const char *wanted = getenv("SCUMMVM_DOS_MODE");
	if (wanted && atoi(wanted) == 640 && setPhysicalMode(640, 480))
		return;

	if (isLauncherContext())
		setLauncherMode();
	else
		setPhysicalMode(kVgaWidth, kVgaHeight);
}

// No game is running (the launcher and its dialogs), as opposed to the
// menu a game shows over itself, which stays in the game's own mode.
bool DOSGraphicsManager::isLauncherContext() {
	return ConfMan.getActiveDomain() == nullptr;
}

// What choosing a mode found (chooseGuiMode(), listDirectModes()) is often decided before the mode is set,
// while the screen is still in text mode: warned once it is in a graphics mode, so that it goes to the log only
void DOSGraphicsManager::logModeNotes() {
	if (!_modeSet)
		return;
	for (uint i = 0; i < _modeNotes.size(); i++)
		warning("%s", _modeNotes[i].c_str());
	_modeNotes.clear();
}

// The menus' resolution: the largest 256 colour mode, up to a limit, that suits the monitor
//
//   dos_gui_mode  (or SCUMMVM_DOS_GUI_MODE)  auto (default) | 320x200 | <width>x<height>
//   dos_gui_max   (or SCUMMVM_DOS_GUI_MAX)   limit for auto, default 1024x768
//   dos_gui_edid  (or SCUMMVM_DOS_EDID)      false/0 stops asking the monitor for its size
//   SCUMMVM_DOS_EDID_SIZE=<width>x<height>   pretends the monitor reported that size
bool DOSGraphicsManager::chooseGuiMode(int &width, int &height) {
	if (_guiModeChosen) {
		width = _guiW;
		height = _guiH;
		return _guiModeValid;
	}
	_guiModeChosen = true;
	_guiModeValid = false;

	Common::String setting("auto");
	if (ConfMan.hasKey("dos_gui_mode"))
		setting = ConfMan.get("dos_gui_mode");
	if (const char *env = getenv("SCUMMVM_DOS_GUI_MODE"))
		setting = env;
	setting.toLowercase();

	if (setting == "320x200" || setting == "vga" || setting == "off" || setting == "small") {
		_modeNotes.push_back(Common::String::format("DOSGFX: menus use VGA mode 13h (320x200) as configured"));
		return false;
	}

	int explicitW = 0, explicitH = 0;
	bool wantExplicit = setting != "auto" && dosParseSize(setting.c_str(), explicitW, explicitH);
	if (setting != "auto" && !wantExplicit)
		_modeNotes.push_back(Common::String::format("DOSGFX: dos_gui_mode '%s' is not 'auto' or a size like 800x600; using auto", setting.c_str()));

	int maxW = 1024, maxH = 768;
	Common::String maxSetting;
	if (ConfMan.hasKey("dos_gui_max"))
		maxSetting = ConfMan.get("dos_gui_max");
	if (const char *env = getenv("SCUMMVM_DOS_GUI_MAX"))
		maxSetting = env;
	if (!maxSetting.empty() && !dosParseSize(maxSetting.c_str(), maxW, maxH)) {
		_modeNotes.push_back(Common::String::format("DOSGFX: dos_gui_max '%s' is not a size like 1024x768; using 1024x768", maxSetting.c_str()));
		maxW = 1024;
		maxH = 768;
	}
	if (maxW > kGuiMaxWidth)
		maxW = kGuiMaxWidth;
	if (maxH > kGuiMaxHeight)
		maxH = kGuiMaxHeight;

	// What the monitor says it is, for the log (listFittingModes() leaves out the modes larger than that)
	int nativeW = 0, nativeH = 0;
	const char *monitorNote = nullptr;
	const bool haveNative = VesaDisplay::monitorSize(nativeW, nativeH, &monitorNote);
	if (*monitorNote)
		_modeNotes.push_back(Common::String::format("DOSGFX: %s", monitorNote));

	DosModeSize modes[64];
	int vbeVersion = 0;
	int count = listFittingModes(modes, 64, &vbeVersion);
	if (!count) {
		_modeNotes.push_back(Common::String::format("DOSGFX: no 256 colour VESA modes (VBE %d.%d); menus use VGA mode 13h",
		                     vbeVersion >> 8, vbeVersion & 0xFF));
		return false;
	}

	Common::String list;
	for (int i = 0; i < count && i < 40; i++)
		list += Common::String::format(" %dx%d", modes[i].w, modes[i].h);
	_modeNotes.push_back(Common::String::format("DOSGFX: VBE %d.%d, 256 colour modes:%s", vbeVersion >> 8, vbeVersion & 0xFF, list.c_str()));
	_modeNotes.push_back(Common::String::format("DOSGFX: the card reports %d KB of video memory", VesaDisplay::videoMemoryKB()));

	// A size asked for by name, if the card has it
	if (wantExplicit) {
		for (int i = 0; i < count; i++) {
			if (modes[i].w == explicitW && modes[i].h == explicitH &&
			        explicitW <= kGuiMaxWidth && explicitH <= kGuiMaxHeight) {
				_guiW = explicitW;
				_guiH = explicitH;
				_guiModeValid = true;
				#ifdef USE_RGB_COLOR
				chooseGuiDepth();
				#endif
				_modeNotes.push_back(Common::String::format("DOSGFX: menus use %dx%d as configured", _guiW, _guiH));
				width = _guiW;
				height = _guiH;
				return true;
			}
		}
		_modeNotes.push_back(Common::String::format("DOSGFX: no %dx%d 256 colour mode that the card and the monitor can show; choosing one", explicitW, explicitH));
	}

	DosModeSize best;
	if (!dosChooseGuiMode(modes, count, maxW, maxH, haveNative, nativeW, nativeH, best)) {
		_modeNotes.push_back(Common::String::format("DOSGFX: no 256 colour mode suits (limit %dx%d); menus use VGA mode 13h", maxW, maxH));
		return false;
	}

	_guiW = best.w;
	_guiH = best.h;
	_guiModeValid = true;
	#ifdef USE_RGB_COLOR
	chooseGuiDepth();
	#endif
	_modeNotes.push_back(Common::String::format("DOSGFX: menus use %dx%d (limit %dx%d)", _guiW, _guiH, maxW, maxH));
	width = _guiW;
	height = _guiH;
	return true;
}

bool DOSGraphicsManager::setLauncherMode() {
	int w, h;
	if (chooseGuiMode(w, h)) {
		#ifdef USE_RGB_COLOR
		// High colour menus: the theme is drawn in its own colours instead of 256
		if (_guiBpp == 16) {
			for (int i = 0; i < _directCount; i++) {
				if (_direct[i].width == w && _direct[i].height == h && _direct[i].redPos == _guiRedPos) {
					if (setPhysicalDirect(_direct[i]))
						return true;
					break;
				}
			}
			_modeNotes.push_back(Common::String::format("DOSGFX: could not set the %dx%d high colour menu mode; using 256 colours", w, h));
			_guiBpp = 8;
		}
		#endif
		if (setPhysicalMode(w, h))
			return true;

		_modeNotes.push_back(Common::String::format("DOSGFX: could not set the menu mode %dx%d; using 320x200", w, h));
		_guiModeValid = false; // do not try again
	}
	return setPhysicalMode(kVgaWidth, kVgaHeight);
}

int DOSGraphicsManager::listFittingModes(DosModeSize *modes, int maxCount, int *vbeVersion) {
	const int count = VesaDisplay::listModes(modes, maxCount, vbeVersion);
	int monitorW, monitorH;
	const bool haveMonitor = VesaDisplay::monitorSize(monitorW, monitorH);
	const long memory = (long)VesaDisplay::videoMemoryKB() * 1024;

	Common::String refused;
	int kept = 0;
	for (int i = 0; i < count; i++) {
		if (dosModeFits(modes[i].w, modes[i].h, 0, 1, haveMonitor, monitorW, monitorH, memory))
			modes[kept++] = modes[i];
		else
			refused += Common::String::format(" %dx%d", modes[i].w, modes[i].h);
	}
	if (!_fittingNoted) {
		_fittingNoted = true;
		noteRefusedModes(refused, haveMonitor, monitorW, monitorH, memory);
	}
	return kept;
}

void DOSGraphicsManager::noteRefusedModes(const Common::String &refused, bool haveMonitor, int monitorW, int monitorH, long videoMemory) {
	if (refused.empty())
		return;
	const Common::String monitor = haveMonitor ? Common::String::format("%dx%d", monitorW, monitorH) : Common::String("unknown");
	const long memoryKB = videoMemory / 1024;
	_modeNotes.push_back(Common::String::format("DOSGFX: modes too large for the monitor (%s) or the video memory (%ld KB):%s", monitor.c_str(), memoryKB, refused.c_str()));
}

void DOSGraphicsManager::restoreTextMode() {
	if (!_modeSet)
		return;

	_vesa.release();
	_useVesa = false;
	_flipping = false;

	__dpmi_regs r;
	r.x.ax = 0x0003; // BIOS: set video mode 3, 80x25 text
	__dpmi_int(0x10, &r);
	_modeSet = false;
}

void DOSGraphicsManager::writeDAC(const byte *rgb8, uint start, uint num) {
	outportb(0x3C8, start);
	for (uint i = 0; i < num * 3; i++)
		outportb(0x3C9, rgb8[start * 3 + i] >> 2); // DAC takes 6 bits
}

// Fixed 3-3-2 palette used while the overlay is visible
void DOSGraphicsManager::writeOverlayDAC() {
	outportb(0x3C8, 0);
	for (int i = 0; i < 256; i++) {
		uint8 r, g, b;
		dosPalette332(i, r, g, b);
		outportb(0x3C9, r);
		outportb(0x3C9, g);
		outportb(0x3C9, b);
	}
}

// Features

bool DOSGraphicsManager::hasFeature(OSystem::Feature f) const {
	if (f == OSystem::kFeatureNoTrueColor) {
		// Without a direct colour mode, high colour is only dithered: no true colour options
		#ifdef USE_RGB_COLOR
		const_cast<DOSGraphicsManager *>(this)->listDirectModes();
		return _directCount == 0;
		#else
		return true;
		#endif
	}
	return f == OSystem::kFeatureCursorPalette;
}

void DOSGraphicsManager::setFeatureState(OSystem::Feature f, bool enable) {
	if (f == OSystem::kFeatureCursorPalette) {
		_cursorPaletteEnabled = enable;
		_cursorMapDirty = true;
		_cursorDirty = true;
	}
}

bool DOSGraphicsManager::getFeatureState(OSystem::Feature f) const {
	return f == OSystem::kFeatureCursorPalette && _cursorPaletteEnabled;
}

#ifdef USE_RGB_COLOR
Graphics::PixelFormat DOSGraphicsManager::getScreenFormat() const {
	if (_screen.getPixels())
		return _screen.format;
	return Graphics::PixelFormat::createFormatCLUT8();
}

// The formats the card can show, best first: its high colour ones (RGB565 ahead
// of RGB555), then 256 colours, which every card has.
Common::List<Graphics::PixelFormat> DOSGraphicsManager::getSupportedFormats() const {
	const_cast<DOSGraphicsManager *>(this)->listDirectModes();

	Common::List<Graphics::PixelFormat> list;
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < _directCount; i++) {
			// First the modes with six green bits (565), then the rest
			if ((_direct[i].greenSize == 6) != (pass == 0))
				continue;

			Graphics::PixelFormat f = modeFormat(_direct[i]);
			bool seen = false;
			for (const auto &g : list) {
				if (g == f)
					seen = true;
			}
			if (!seen)
				list.push_back(f);
		}
	}

	list.push_back(Graphics::PixelFormat::createFormatCLUT8());

	// Formats the card cannot show are dithered, and come last so engines prefer the others
	if (ditherAllowed()) {
		static const Graphics::PixelFormat shown[] = {
			Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0), // RGB565
			Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0)  // RGB555
		};
		for (const auto &f : shown) {
			bool seen = false;
			for (const auto &g : list) {
				if (g == f)
					seen = true;
			}
			if (!seen)
				list.push_back(f);
		}
	}
	return list;
}
#endif

// Screen size / transactions

void DOSGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	_pending = true;
	_pendingW = width;
	_pendingH = height;
	#ifdef USE_RGB_COLOR
	// 256 colours is always possible; a high colour format only if the card
	// has a mode laid out that way
	_pendingFormat = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
	_pendingFormatOk = true;
	if (format && format->bytesPerPixel != 1) {
		listDirectModes();
		_pendingFormatOk = false;
		for (int i = 0; i < _directCount; i++) {
			if (modeFormat(_direct[i]) == *format)
				_pendingFormatOk = true;
		}

		// Or a 16-bit format that can be shown dithered in 256 colours
		if (!_pendingFormatOk && ditherAllowed() && format->bytesPerPixel == 2 &&
		        format->rBits() >= 5 && format->gBits() >= 5 && format->bBits() >= 5)
			_pendingFormatOk = true;
	}
	#else
	_pendingFormatOk = !format || format->bytesPerPixel == 1;
	#endif
}

void DOSGraphicsManager::beginGFXTransaction() {
	_pending = false;
	_pendingFormatOk = true;
}

OSystem::TransactionError DOSGraphicsManager::endGFXTransaction() {
	if (!_pending)
		return OSystem::kTransactionSuccess;
	_pending = false;

	if (!_pendingFormatOk)
		return OSystem::kTransactionFormatNotSupported;

	#ifdef USE_RGB_COLOR
	if (_pendingFormat.bytesPerPixel == 2)
		return endDeepTransaction();
	_dither = false; // an ordinary 256 colour game: the game's own palette
	#endif

	if (_pendingW == 0 || _pendingH == 0 || _pendingW > kGuiMaxWidth || _pendingH > kGuiMaxHeight)
		return OSystem::kTransactionSizeChangeFailed;

	// 320x200 or smaller fits VGA mode 13h; anything up to 640x480 (the
	// SCUMM v7 and v8 games) needs a VESA mode
	bool small = _pendingW <= (uint)kVgaWidth && _pendingH <= (uint)kVgaHeight;

	// SCUMMVM_DOS_MODE=640 uses the 640x480 mode even for small games, which
	// are then centred in it: for trying that mode without a v7 or v8 game
	const char *forced = getenv("SCUMMVM_DOS_MODE");
	if (forced && atoi(forced) == 640)
		small = false;

	// Larger still (SCUMM's Hercules mode is 720x350): the smallest VESA mode the card has that holds it
	int physW = small ? kVgaWidth : kMaxWidth;
	int physH = small ? kVgaHeight : kMaxHeight;
	if (_pendingW > kMaxWidth || _pendingH > kMaxHeight) {
		DosModeSize modes[64];
		DosModeSize best = { 0, 0 };
		int count = listFittingModes(modes, 64);
		if (!dosChooseGameMode(modes, count, _pendingW, _pendingH, kGuiMaxWidth, kGuiMaxHeight, best)) {
			warning("DOSGFX: no 256 colour VESA mode holds a %ux%u screen", _pendingW, _pendingH);
			return OSystem::kTransactionSizeChangeFailed;
		}
		physW = best.w;
		physH = best.h;
	}

	// The launcher asks for 320x200 too, but gets the GUI mode (see chooseGuiMode())
	bool modeOk;
	if (small && isLauncherContext())
		modeOk = setLauncherMode();
	else
		modeOk = setPhysicalMode(physW, physH);
	if (!modeOk)
		return OSystem::kTransactionSizeChangeFailed;

	_screen.free();
	_screen.create(_pendingW, _pendingH, Graphics::PixelFormat::createFormatCLUT8());
	memset(_screen.getPixels(), 0, _screen.pitch * _screen.h);

	_offX = (_physW - (int)_pendingW) / 2;
	_offY = (_physH - (int)_pendingH) / 2;

	// The mode is the new game's: hideOverlay() must not put a 320x200 one back
	_overlayRaised = false;

	markAllDirty();
	_cursorDirty = true;

	_screenChangeID++;
	return OSystem::kTransactionSuccess;
}

int16 DOSGraphicsManager::getHeight() const {
	return _screen.h;
}

int16 DOSGraphicsManager::getWidth() const {
	return _screen.w;
}

// Palette

void DOSGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	if (start >= 256)
		return;
	if (start + num > 256)
		num = 256 - start;

	memcpy(_palette + start * 3, colors, num * 3);
	_cursorMapDirty = true;

	// The DAC takes care of the screen; only a cursor whose colours depend
	// on this palette has to be redrawn.
	if (_cursorPaletteEnabled || _overlayVisible)
		_cursorDirty = true;

	// Written in updateScreen(), after the new pixels: the DAC would otherwise
	// show the previous picture in the new colours
	if (_modeSet && !_overlayVisible)
		_palettePending = true;
}

void DOSGraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
	if (start >= 256)
		return;
	if (start + num > 256)
		num = 256 - start;

	memcpy(colors, _palette + start * 3, num * 3);
}

// Dirty rectangles

void DOSGraphicsManager::addDirty(const Common::Rect &rect) {
	if (_dirtyAll)
		return;

	Common::Rect r = rect;
	r.clip(_physW, _physH);
	if (r.isEmpty())
		return;

	// Already covered, or covers one we have
	for (int i = 0; i < _dirtyCount; i++) {
		if (_dirty[i].contains(r))
			return;
		if (r.contains(_dirty[i])) {
			_dirty[i] = r;
			return;
		}
	}

	if (_dirtyCount == kMaxDirty) {
		// Too many: replace them all by their bounding box
		for (int i = 1; i < _dirtyCount; i++)
			_dirty[0].extend(_dirty[i]);
		_dirty[0].extend(r);
		_dirtyCount = 1;
		return;
	}

	_dirty[_dirtyCount++] = r;
}

Common::Rect DOSGraphicsManager::cursorRect() const {
	if (!_cursorVisible || !_cursor)
		return Common::Rect();

	int x = _hostMouseX - _hotX;
	int y = _hostMouseY - _hotY;
	Common::Rect r(x, y, x + (int)_cursorW, y + (int)_cursorH);
	r.clip(_physW, _physH);
	if (r.isEmpty())
		return Common::Rect();
	return r;
}

// Drawing

void DOSGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	clearBanner();
	if (!_screen.getPixels())
		return;
	_screen.copyRectToSurface(buf, pitch, x, y, w, h);

	if (!_overlayVisible)
	#ifdef USE_RGB_COLOR
		addDirty(gameRectToPhysical(Common::Rect(x, y, x + w, y + h)));
	#else
		addDirty(Common::Rect(x + _offX, y + _offY, x + _offX + w, y + _offY + h));
	#endif
}

Graphics::Surface *DOSGraphicsManager::lockScreen() {
	return &_screen;
}

void DOSGraphicsManager::unlockScreen() {
	clearBanner();
	// The caller may have drawn anywhere
	if (!_overlayVisible)
		markAllDirty();
}

void DOSGraphicsManager::fillScreen(uint32 col) {
	clearBanner();
	if (!_screen.getPixels())
		return;
	_screen.fillRect(Common::Rect(0, 0, _screen.w, _screen.h), col);

	if (!_overlayVisible)
		markAllDirty();
}

void DOSGraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	clearBanner();
	if (!_screen.getPixels())
		return;
	_screen.fillRect(r, col);

	if (!_overlayVisible) {
		#ifdef USE_RGB_COLOR
		addDirty(gameRectToPhysical(r));
		#else
		Common::Rect d = r;
		d.translate(_offX, _offY);
		addDirty(d);
		#endif
	}
}

/** Render the game screen, or the overlay, for the pixels in r. */
void DOSGraphicsManager::renderRect(byte *dst, const Common::Rect &r) {
	#ifdef USE_RGB_COLOR
	if (_physBpp == 2) {
		renderRect16(dst, r);
		return;
	}
	if (_dither && !_overlayVisible) {
		renderRectDither(dst, r);
		return;
	}
	#endif

	if (_overlayVisible) {
		for (int y = r.top; y < r.bottom; y++) {
			const uint16 *src = (const uint16 *)_overlay.getBasePtr(0, y);
			byte *d = dst + y * _physW;
			for (int x = r.left; x < r.right; x++) {
				uint16 p = src[x];
				byte r3 = (p >> 11) >> 2;         // 5 bits -> top 3
				byte g3 = ((p >> 5) & 0x3F) >> 3; // 6 bits -> top 3
				byte b2 = (p & 0x1F) >> 3;        // 5 bits -> top 2
				d[x] = (r3 << 5) | (g3 << 2) | b2;
			}
		}
		return;
	}

	for (int y = r.top; y < r.bottom; y++) {
		byte *d = dst + y * _physW;
		int gy = y - _offY;

		// Outside the game screen (when it is smaller than 320x200): black
		if (!_screen.getPixels() || gy < 0 || gy >= _screen.h) {
			memset(d + r.left, 0, r.width());
			continue;
		}

		int gx0 = MAX<int>(r.left, _offX);
		int gx1 = MIN<int>(r.right, _offX + _screen.w);
		if (gx0 >= gx1) {
			memset(d + r.left, 0, r.width());
			continue;
		}

		if (gx0 > r.left)
			memset(d + r.left, 0, gx0 - r.left);
		memcpy(d + gx0, _screen.getBasePtr(gx0 - _offX, gy), gx1 - gx0);
		if (gx1 < r.right)
			memset(d + gx1, 0, r.right - gx1);
	}
}

void DOSGraphicsManager::rebuildCursorMap() {
	_cursorMapDirty = false;

	if (_overlayVisible) {
		// Everything on screen is 3-3-2 now
		const byte *pal = _cursorPaletteEnabled ? _cursorPalette : _palette;
		for (int i = 0; i < 256; i++)
			_cursorMap[i] = dosRgbTo332(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]);
	}
	#ifdef USE_RGB_COLOR
	else if (_dither) {
		// A high colour game on a 256 colour screen: the colour cube
		const byte *pal = _cursorPaletteEnabled ? _cursorPalette : _palette;
		for (int i = 0; i < 256; i++)
			_cursorMap[i] = _plainR[pal[i * 3]] + _plainG[pal[i * 3 + 1]] + _plainB[pal[i * 3 + 2]];
	}
	#endif
	else if (_cursorPaletteEnabled) {
		// The cursor has its own colours; find the closest ones in the
		// game palette, since VGA has one palette for everything.
		for (int i = 0; i < 256; i++) {
			int best = 0;
			int bestDist = 0x7FFFFFFF;
			for (int j = 0; j < 256; j++) {
				int dr = _cursorPalette[i * 3] - _palette[j * 3];
				int dg = _cursorPalette[i * 3 + 1] - _palette[j * 3 + 1];
				int db = _cursorPalette[i * 3 + 2] - _palette[j * 3 + 2];
				int dist = dr * dr + dg * dg + db * db;
				if (dist < bestDist) {
					bestDist = dist;
					best = j;
					if (!dist)
						break;
				}
			}
			_cursorMap[i] = best;
		}
	} else {
		for (int i = 0; i < 256; i++)
			_cursorMap[i] = i;
	}
}

/** Draw the cursor into dst, but only the part inside clip. */
void DOSGraphicsManager::drawCursor(byte *dst, const Common::Rect &clip) {
	if (!_cursorVisible || !_cursor)
		return;

	#ifdef USE_RGB_COLOR
	if (_physBpp == 2) {
		drawCursor16(dst, clip);
		return;
	}
	if (_cursorBpp != 1) {
		// A high colour cursor: only a high colour game on a 256 colour screen has one
		if (_dither && !_overlayVisible)
			drawCursorDithered(dst, clip);
		return;
	}
	#endif

	if (_cursorMapDirty)
		rebuildCursorMap();

	int ox = _hostMouseX - _hotX;
	int oy = _hostMouseY - _hotY;

	int y0 = MAX<int>(clip.top, oy);
	int y1 = MIN<int>(clip.bottom, oy + (int)_cursorH);
	int x0 = MAX<int>(clip.left, ox);
	int x1 = MIN<int>(clip.right, ox + (int)_cursorW);

	for (int y = y0; y < y1; y++) {
		const byte *src = _cursor + (y - oy) * _cursorW + (x0 - ox);
		byte *d = dst + y * _physW;
		for (int x = x0; x < x1; x++, src++) {
			byte c = *src;
			if ((uint32)c == _keycolor)
				continue;
			d[x] = _cursorMap[c];
		}
	}
}

/** Copy the part of _composite inside r to video memory. */
void DOSGraphicsManager::blitRect(const Common::Rect &r) {
	#ifdef USE_RGB_COLOR
	if (_physBpp == 2) {
		// Two bytes a pixel: the same byte-wise writer, with the rectangle in bytes
		_vesa.putRect(_composite + ((size_t)r.top * _physW + r.left) * 2, _physW * 2,
		              r.left * 2, r.top, r.width() * 2, r.height());
		return;
	}
	#endif

	if (_useVesa) {
		_vesa.putRect(_composite + r.top * _physW + r.left, _physW,
		              r.left, r.top, r.width(), r.height());
		return;
	}

	if (r.left == 0 && r.right == _physW) {
		// Whole rows are contiguous: one copy
		dosmemput(_composite + r.top * _physW, r.height() * _physW,
		          kVideoMemory + r.top * _physW);
		return;
	}

	for (int y = r.top; y < r.bottom; y++) {
		dosmemput(_composite + y * _physW + r.left, r.width(),
		          kVideoMemory + y * _physW + r.left);
	}
}

// Redraw everything from scratch and compare with what is in video memory.
void DOSGraphicsManager::verifyFrame() {
	#ifdef USE_RGB_COLOR
	if (_physBpp == 2) {
		verifyFrame16();
		return;
	}
	#endif

	const Common::Rect full(0, 0, _physW, _physH);

	renderRect(_verifyBuf, full);
	drawCursor(_verifyBuf, full);
	if (_useVesa)
		_vesa.readAll(_vramBuf);
	else
		dosmemget(kVideoMemory, _physW * _physH, _vramBuf);

	_verifyFrames++;

	if (memcmp(_verifyBuf, _vramBuf, (_physW * _physH)) != 0) {
		_verifyBad++;
		if (_verifyBad <= 5) {
			int i = 0;
			int count = 0;
			for (int p = 0; p < (_physW * _physH); p++) {
				if (_verifyBuf[p] != _vramBuf[p]) {
					if (!count)
						i = p;
					count++;
				}
			}
			warning("DOSGFX verify: frame %u differs in %d pixels, first at x=%d y=%d (expected %u, video memory has %u)",
			        _verifyFrames, count, i % _physW, i / _physW, _verifyBuf[i], _vramBuf[i]);
		}
	}

	if (_verifyFrames == 20 || _verifyFrames == 200 || _verifyFrames == 1000 || _verifyFrames % 5000 == 0)
		warning("DOSGFX verify: %u frames checked, %u differed (%dx%d%s%s)", _verifyFrames, _verifyBad,
		        _physW, _physH, _useVesa ? (_vesa.isLinear() ? ", VESA linear" : ", VESA banked") : ", VGA",
		        _overlayVisible ? ", overlay" : "");
}

// A line of text for the person at the screen, drawn by the program itself: the
// BIOS would draw it at 8x8 pixels. The BIOS font is read from the card.
bool DOSGraphicsManager::loadRomFont() {
	if (_romFontLoaded)
		return true;

	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x1130; // get font information
	r.h.bh = 3;      // the ROM 8x8 font
	__dpmi_int(0x10, &r);
	unsigned long address = ((unsigned long)r.x.es << 4) + r.x.bp;
	if (!address)
		return false;

	dosmemget(address, sizeof(_romFont), _romFont);
	_romFontLoaded = true;
	return true;
}

Common::Rect DOSGraphicsManager::bannerRect() const {
	if (_banner.empty())
		return Common::Rect();

	int lines = 0, widest = 0, run = 0;
	for (uint i = 0; i < _banner.size(); i++) {
		if (_banner[i] == '\n') {
			lines++;
			widest = MAX(widest, run);
			run = 0;
		} else {
			run++;
		}
	}
	const DosBannerLayout &l = _bannerLayout;
	return Common::Rect(l.margin, l.margin, l.margin + widest * l.glyph, l.margin + lines * l.lineStep);
}

void DOSGraphicsManager::showBanner(const char *text) {
	if (!_modeSet || !loadRomFont())
		return;

	const DosBannerLayout layout = dosBannerLayout(_physW, _physH);
	char wrapped[512];
	if (!dosBannerWrap(text, layout.perLine, layout.maxLines, wrapped, sizeof(wrapped)))
		return;

	clearBanner();
	_banner = wrapped;
	_bannerLayout = layout;
	// Before a game starts, the banner goes on a blank screen
	_bannerBlank = g_engine == nullptr;
	if (_bannerBlank)
		markAllDirty();
	else
		addDirty(bannerRect());

	// 256 colours: a palette entry of its own, put back with the next palette
	// write. Written after the update, which brings the game's palette.
	updateScreen();
	#ifdef USE_RGB_COLOR
	if (_physBpp == 1)
	#endif
	{
		outportb(0x3C8, 255);
		outportb(0x3C9, 0);
		outportb(0x3C9, 63);
		outportb(0x3C9, 0);
		_palettePending = true;
		_dacKnown = false;
	}
}

void DOSGraphicsManager::clearBanner() {
	if (_banner.empty())
		return;
	const Common::Rect r = bannerRect();
	_banner.clear();
	if (_modeSet) {
		if (_bannerBlank)
			markAllDirty();
		else
			addDirty(r);
	}
	_bannerBlank = false;
}

/** Draw the banner into dst, but only the part inside clip. */
void DOSGraphicsManager::drawBanner(byte *dst, const Common::Rect &clip) {
	if (_banner.empty() || !_romFontLoaded)
		return;

	int bpp = 1;
	uint32 color = 255;
	#ifdef USE_RGB_COLOR
	if (_physBpp == 2) {
		bpp = 2;
		color = _physFormat.RGBToColor(0, 255, 0);
	}
	#endif

	if (_bannerBlank) {
		for (int y = clip.top; y < clip.bottom; y++)
			memset(dst + ((size_t)y * _physW + clip.left) * bpp, 0, (size_t)clip.width() * bpp);
	}

	const DosBannerLayout &l = _bannerLayout;
	int y0 = l.margin;
	int x0 = l.margin;
	int x = x0;
	int y = y0;
	for (uint i = 0; i < _banner.size(); i++) {
		const char c = _banner[i];
		if (c == '\n') {
			x = x0;
			y += l.lineStep;
			continue;
		}

		const uint8 *glyph = _romFont + (uint8)c * 8;
		for (int row = 0; row < 8; row++) {
			for (int col = 0; col < 8; col++) {
				if (!(glyph[row] & (0x80 >> col)))
					continue;
				for (int dy = 0; dy < l.scale; dy++) {
					const int py = y + row * l.scale + dy;
					if (py < clip.top || py >= clip.bottom)
						continue;
					for (int dx = 0; dx < l.scale; dx++) {
						const int px = x + col * l.scale + dx;
						if (px < clip.left || px >= clip.right)
							continue;
						if (bpp == 1) {
							dst[py * _physW + px] = (byte)color;
						} else {
							byte *q = dst + ((size_t)py * _physW + px) * 2;
							q[0] = color & 0xFF;
							q[1] = (color >> 8) & 0xFF;
						}
					}
				}
			}
		}
		x += l.glyph;
	}
}

// Page flipping: draw the next picture to a page that is not on the display, then
// show it. dos_pageflip=false (or SCUMMVM_DOS_PAGEFLIP=0) leaves it off.
bool DOSGraphicsManager::pageFlipWanted() const {
	bool wanted = true;
	if (ConfMan.hasKey("dos_pageflip"))
		wanted = ConfMan.getBool("dos_pageflip");
	if (const char *env = getenv("SCUMMVM_DOS_PAGEFLIP"))
		wanted = wanted && atoi(env) != 0;
	return wanted;
}

// After a VESA mode is set
void DOSGraphicsManager::setupPageFlipping() {
	_flipping = false;
	_prevBox = Common::Rect();
	const int pages = _vesa.setupPages();
	if (pages >= 2) {
		_flipping = true;
		warning("DOSGFX: %d display pages: drawing to a hidden page, switching at the vertical retrace", pages);
	} else if (pageFlipWanted()) {
		warning("DOSGFX: no page flipping in this mode (too little video memory, or the card cannot switch pages)");
	}
}

// Wait for the start of the next vertical retrace. The loops are bounded: a
// display adapter that never reports it must not hang the program.
void DOSGraphicsManager::waitRetrace() {
	unsigned n = 0;
	while ((inportb(0x3DA) & 8) && ++n < 400000) {
	}
	n = 0;
	while (!(inportb(0x3DA) & 8) && ++n < 800000) {
	}
}

// Write the palette the screen is meant to have now
void DOSGraphicsManager::applyPalette() {
	_palettePending = false;
	if (_overlayVisible)
		writeOverlayDAC();
	#ifdef USE_RGB_COLOR
	else if (_dither) {
		writeDitherDAC();
		_dacKnown = false;
	}
	#endif
	else {
		writeDAC(_palette, 0, 256);
		_dacKnown = true;
	}
}

// The picture goes straight to the display (VGA mode 13h, or a mode with room
// for one page only)
void DOSGraphicsManager::presentDirect(bool big, bool blank) {
	// A fast card gets the new palette at the retrace, then the pixels, ahead of the beam;
	// a slow one gets it after the pixels, so the old picture never shows in new colours
	const uint64 fastBlit = UCLOCKS_PER_SEC / 80; // 12.5 ms, a frame at 60 Hz is 16.7
	bool paletteFirst = _palettePending && !blank && !_overlayVisible && _bigBlitTicks != 0 &&
	                    _bigBlitTicks < fastBlit;
	#ifdef USE_RGB_COLOR
	if (_dither)
		paletteFirst = false;
	#endif
	if (big || paletteFirst)
		waitRetrace();
	if (paletteFirst) {
		_palettePending = false;
		writeDAC(_palette, 0, 256);
		_dacKnown = true;
	}

	const uint64 blitStart = big ? (uint64)uclock() : 0;
	if (_dirtyAll) {
		const Common::Rect full(0, 0, _physW, _physH);
		renderRect(_composite, full);
		drawCursor(_composite, full);
		drawBanner(_composite, full);
		blitRect(full);
	} else {
		for (int i = 0; i < _dirtyCount; i++) {
			renderRect(_composite, _dirty[i]);
			drawCursor(_composite, _dirty[i]);
			drawBanner(_composite, _dirty[i]);
			blitRect(_dirty[i]);
		}
	}
	if (big)
		_bigBlitTicks = MAX<uint64>((uint64)uclock() - blitStart, 1);
	_dirtyAll = false;
	_dirtyCount = 0;

	if (_palettePending)
		applyPalette();
}

// Page flipping: draw to the hidden page, then show it with the new palette at the retrace
void DOSGraphicsManager::presentFlipped() {
	if (!_dirtyAll && _dirtyCount == 0) {
		// Nothing to draw: a palette change alone (a fade, colour cycling)
		if (_palettePending) {
			waitRetrace();
			applyPalette();
		}
		return;
	}

	const Common::Rect full(0, 0, _physW, _physH);
	Common::Rect box;

	if (_dirtyAll) {
		renderRect(_composite, full);
		drawCursor(_composite, full);
		drawBanner(_composite, full);
		box = full;
	} else {
		for (int i = 0; i < _dirtyCount; i++) {
			renderRect(_composite, _dirty[i]);
			drawCursor(_composite, _dirty[i]);
			drawBanner(_composite, _dirty[i]);
			if (box.isEmpty())
				box = _dirty[i];
			else
				box.extend(_dirty[i]);
		}
	}

	// The page to draw to was on the display until the last flip took effect
	if ((uint64)uclock() - _lastFlip < UCLOCKS_PER_SEC / 50)
		waitRetrace();

	// This update's changes, and what the last one changed on the other page
	if (_dirtyAll) {
		blitRect(full);
	} else {
		for (int i = 0; i < _dirtyCount; i++)
			blitRect(_dirty[i]);
		if (!_prevBox.isEmpty())
			blitRect(_prevBox);
	}

	_dirtyAll = false;
	_dirtyCount = 0;

	const bool flipped = _vesa.flip();

	if (!flipped) {
		// The card gave up page flipping: one page, and all of it drawn again
		_flipping = false;
		_prevBox = Common::Rect();
		markAllDirty();
		warning("DOSGFX: the card did not switch display pages; page flipping is off");
		return;
	}
	_lastFlip = (uint64)uclock();
	_prevBox = box;

	if (_palettePending)
		applyPalette();
}

void DOSGraphicsManager::updateScreen() {
	if (!_modeSet)
		return;

	if (_verify) {
		// Jump the cursor around so erasing and redrawing it is exercised
		unsigned n = _verifyFrames + 1;
		_hostMouseX = 10 + (n * 37) % 300;
		_hostMouseY = 10 + (n * 23) % 180;
		if (_cursor)
			_cursorVisible = true;
	}

	// The cursor moved or changed: redraw where it was and where it is
	Common::Rect cur = cursorRect();
	if (_cursorDirty || !(cur == _lastCursorRect)) {
		addDirty(_lastCursorRect);
		addDirty(cur);
		_cursorDirty = false;
	}
	_lastCursorRect = cur;

	// How much of the screen this update redraws
	long area = _dirtyAll ? (long)_physW * _physH : 0;
	for (int i = 0; !_dirtyAll && i < _dirtyCount; i++)
		area += (long)_dirty[i].width() * _dirty[i].height();
	const bool big = area >= (long)_physW * _physH / 2;

	#ifdef USE_RGB_COLOR
	// A direct colour mode has no palette to write
	if (_physBpp == 2)
		_palettePending = false;
	#endif

	// After an overlay or mode switch the screen holds the other picture's
	// pixels: show black until the new ones are drawn, then the new colours.
	const bool blank = _palettePending && !_dacKnown;
	if (blank) {
		static const byte black[256 * 3] = { 0 };
		writeDAC(black, 0, 256);
	}

	if (_flipping) {
		presentFlipped();
	} else {
		presentDirect(big, blank);
	}

	if (_verify)
		verifyFrame();

	if (_shotInterval && (uint64)uclock() >= _shotNext) {
		saveScreenshot();
		_shotNext = (uint64)uclock() + _shotInterval;
	}
}

void DOSGraphicsManager::saveScreenshot() {
	if (!_modeSet || _shotCount >= 9999)
		return;

	#ifdef USE_RGB_COLOR
	if (_physBpp == 2) {
		saveScreenshot16();
		return;
	}
	#endif

	char name[16];
	_shotCount++;
	snprintf(name, sizeof(name), "shot%04u.bmp", _shotCount);

	FILE *f = fopen(name, "wb");
	if (!f)
		return;

	// 8-bit BMP: file header, info header, 256 BGRA palette entries, rows
	// stored bottom to top, each padded to a multiple of 4 bytes
	const int w = _physW, h = _physH;
	const int rowBytes = (w + 3) & ~3;
	const unsigned dataOffset = 14 + 40 + 1024;

	uint8 header[14 + 40];
	memset(header, 0, sizeof(header));
	header[0] = 'B';
	header[1] = 'M';
	WRITE_LE_UINT32(header + 2, dataOffset + rowBytes * h);
	WRITE_LE_UINT32(header + 10, dataOffset);
	WRITE_LE_UINT32(header + 14, 40);          // size of the info header
	WRITE_LE_UINT32(header + 18, w);
	WRITE_LE_UINT32(header + 22, h);
	WRITE_LE_UINT16(header + 26, 1);           // planes
	WRITE_LE_UINT16(header + 28, 8);           // bits per pixel
	WRITE_LE_UINT32(header + 34, rowBytes * h);
	WRITE_LE_UINT32(header + 46, 256);         // colours used
	fwrite(header, 1, sizeof(header), f);

	// The palette the screen shows now: the fixed 3-3-2 one for the overlay,
	// otherwise the game's (the DAC keeps only the top 6 bits of each part)
	uint8 pal[1024];
	#ifdef USE_RGB_COLOR
	uint8 cube[256][3];
	dosDitherPalette(cube);
	#endif
	for (int i = 0; i < 256; i++) {
		uint8 r, g, b;
		if (_overlayVisible) {
			dosPalette332(i, r, g, b);
			r *= 4;
			g *= 4;
			b *= 4;
			#ifdef USE_RGB_COLOR
		} else if (_dither) {
			r = cube[i][0] & 0xFC;
			g = cube[i][1] & 0xFC;
			b = cube[i][2] & 0xFC;
			#endif
		} else {
			r = _palette[i * 3] & 0xFC;
			g = _palette[i * 3 + 1] & 0xFC;
			b = _palette[i * 3 + 2] & 0xFC;
		}
		pal[i * 4 + 0] = b;
		pal[i * 4 + 1] = g;
		pal[i * 4 + 2] = r;
		pal[i * 4 + 3] = 0;
	}
	fwrite(pal, 1, sizeof(pal), f);

	static const uint8 zeros[4] = { 0, 0, 0, 0 };
	for (int y = h - 1; y >= 0; y--) {
		fwrite(_composite + y * w, 1, w, f);
		if (rowBytes > w)
			fwrite(zeros, 1, rowBytes - w, f);
	}
	fclose(f);
}

// Overlay

void DOSGraphicsManager::showOverlay(bool inGUI) {
	ensureVideoMode();
	_overlayVisible = true;
	_cursorMapDirty = true;
	_cursorDirty = true;
	markAllDirty();
	_palettePending = true; // the overlay's colours once its pixels are drawn
	_dacKnown = false;

	// The menus and the debugger console of a 320x200 game are drawn at 640x480,
	// not stretched over 320x200
	bool lowRes = _physW == kVgaWidth && _physH == kVgaHeight && !isLauncherContext();
	#ifdef USE_RGB_COLOR
	lowRes = lowRes && _physBpp == 1 && !_dither;
	#endif
	if (lowRes && !_overlayRaised) {
		if (setPhysicalMode(kMaxWidth, kMaxHeight)) {
			_overlayRaised = true;
			_screenChangeID++; // the GUI lays itself out for the new size
		}
	}
}

void DOSGraphicsManager::hideOverlay() {
	_overlayVisible = false;
	_cursorMapDirty = true;
	_cursorDirty = true;
	markAllDirty();
	_palettePending = true; // the game's colours once its pixels are drawn
	_dacKnown = false;

	if (_overlayRaised) {
		_overlayRaised = false;
		if (setPhysicalMode(kVgaWidth, kVgaHeight) && _screen.getPixels()) {
			_offX = (_physW - _screen.w) / 2;
			_offY = (_physH - _screen.h) / 2;
			_screenChangeID++;
		}
	}
}

Graphics::PixelFormat DOSGraphicsManager::getOverlayFormat() const {
	return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); // RGB565
}

// The game screen, in the overlay's colours, as large a whole multiple as fits
// and centred. Only for a 256 colour game.
void DOSGraphicsManager::drawGameIntoOverlay() {
	if (!_screen.getPixels() || _screen.format.bytesPerPixel != 1 || !_overlay.getPixels() ||
	        _overlay.format.bytesPerPixel != 2)
		return;

	int scale = MIN(_overlay.w / _screen.w, _overlay.h / _screen.h);
	if (scale < 1)
		return;

	uint16 table[256];
	for (int i = 0; i < 256; i++) {
		table[i] = ((_palette[i * 3] >> 3) << 11) | ((_palette[i * 3 + 1] >> 2) << 5) | (_palette[i * 3 + 2] >> 3);
	}

	const int ox = (_overlay.w - _screen.w * scale) / 2;
	const int oy = (_overlay.h - _screen.h * scale) / 2;

	for (int y = 0; y < _screen.h; y++) {
		const byte *src = (const byte *)_screen.getBasePtr(0, y);
		uint16 *first = (uint16 *)_overlay.getBasePtr(ox, oy + y * scale);
		for (int x = 0; x < _screen.w; x++) {
			const uint16 v = table[src[x]];
			for (int k = 0; k < scale; k++)
				first[x * scale + k] = v;
		}
		for (int s = 1; s < scale; s++)
			memcpy((byte *)first + s * _overlay.pitch, first, _screen.w * scale * 2);
	}
}

void DOSGraphicsManager::clearOverlay() {
	memset(_overlay.getPixels(), 0, _overlay.pitch * _overlay.h);

	// A dialog over a running game shows the game behind it, as on other ports
	if (!isLauncherContext())
		drawGameIntoOverlay();

	if (_overlayVisible)
		markAllDirty();
}

void DOSGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	for (int y = 0; y < _physH && y < surface.h; y++) {
		memcpy(surface.getBasePtr(0, y), _overlay.getBasePtr(0, y),
		       _physW * 2 < surface.pitch ? _physW * 2 : surface.pitch);
	}
}

void DOSGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	_overlay.copyRectToSurface(buf, pitch, x, y, w, h);

	if (_overlayVisible)
		addDirty(Common::Rect(x, y, x + w, y + h));
}

// Mouse cursor

bool DOSGraphicsManager::showMouse(bool visible) {
	bool wasVisible = _cursorVisible;
	_cursorVisible = visible;
	_cursorDirty = true;
	return wasVisible;
}

void DOSGraphicsManager::setHostMouse(int x, int y) {
	// updateScreen() notices the cursor has moved
	_hostMouseX = x;
	_hostMouseY = y;
}

void DOSGraphicsManager::getGameMouse(int &x, int &y) const {
	x = _hostMouseX;
	y = _hostMouseY;

	if (_overlayVisible || !_screen.getPixels())
		return;

	x -= _offX;
	y -= _offY;
	#ifdef USE_RGB_COLOR
	if (_scale == 2) {
		x = x < 0 ? 0 : x / 2;
		y = y < 0 ? 0 : y / 2;
	}
	#endif
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= _screen.w)
		x = _screen.w - 1;
	if (y >= _screen.h)
		y = _screen.h - 1;
}

void DOSGraphicsManager::warpMouse(int x, int y) {
	if (!_overlayVisible && _screen.getPixels()) {
		#ifdef USE_RGB_COLOR
		x = x * _scale + _offX;
		y = y * _scale + _offY;
		#else
		x += _offX;
		y += _offY;
		#endif
	}

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= _physW)
		x = _physW - 1;
	if (y >= _physH)
		y = _physH - 1;

	_hostMouseX = x;
	_hostMouseY = y;
	dosMouseSetPosition(x, y);
}

void DOSGraphicsManager::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor, const Graphics::PixelFormat *format, const byte *mask, frac_t scaleX, frac_t scaleY) {
	// The old cursor image has to be erased from where it is
	_cursorDirty = true;

	delete[] _cursor;
	_cursor = nullptr;
	_cursorW = _cursorH = 0;

	#ifdef USE_RGB_COLOR
	// Palettised cursors, and 16-bit ones for a high colour screen
	if (!buf || !w || !h || (format && format->bytesPerPixel != 1 && format->bytesPerPixel != 2))
		return;

	_cursorBpp = format ? format->bytesPerPixel : 1;
	_cursorFormat = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
	_cursor = new byte[w * h * _cursorBpp];
	memcpy(_cursor, buf, w * h * _cursorBpp);
	#else
	// Only palettised cursors can be drawn on an 8-bit screen
	if (!buf || !w || !h || (format && format->bytesPerPixel != 1))
		return;

	_cursor = new byte[w * h];
	memcpy(_cursor, buf, w * h);
	#endif
	_cursorW = w;
	_cursorH = h;
	_hotX = hotspotX;
	_hotY = hotspotY;
	_keycolor = keycolor;
	_cursorMapDirty = true;
}

void DOSGraphicsManager::setCursorPalette(const byte *colors, uint start, uint num) {
	if (start >= 256)
		return;
	if (start + num > 256)
		num = 256 - start;

	memcpy(_cursorPalette + start * 3, colors, num * 3);
	_cursorPaletteEnabled = true;
	_cursorMapDirty = true;
	_cursorDirty = true;
}

#ifdef USE_RGB_COLOR

// High colour (15 and 16 bit): a VESA mode with two bytes a pixel, the game doubled
// when it fits twice. The RGB565 overlay goes straight onto it.
//
//   dos_16bit     (or SCUMMVM_DOS_16BIT)      false/0 turns high colour off (256 colours only)
//   dos_gui_depth (or SCUMMVM_DOS_GUI_DEPTH)  auto (default) | 16 | 8: colour depth of the menus

Graphics::PixelFormat DOSGraphicsManager::modeFormat(const VesaDirectMode &m) {
	return Graphics::PixelFormat(2, m.redSize, m.greenSize, m.blueSize, 0, m.redPos, m.greenPos, m.bluePos, 0);
}

void DOSGraphicsManager::listDirectModes() {
	if (_directListed)
		return;
	_directListed = true;

	bool wanted = true;
	if (ConfMan.hasKey("dos_16bit"))
		wanted = ConfMan.getBool("dos_16bit");
	if (const char *env = getenv("SCUMMVM_DOS_16BIT"))
		wanted = atoi(env) != 0;
	if (!wanted) {
		_modeNotes.push_back("DOSGFX: high colour switched off");
		logModeNotes();
		return;
	}

	_directCount = VesaDisplay::listDirectModes(_direct, 32);

	// Testing aid: SCUMMVM_DOS_DIRECT_MINW=<width> hides the narrower high colour modes
	if (const char *minw = getenv("SCUMMVM_DOS_DIRECT_MINW")) {
		int kept = 0;
		for (int i = 0; i < _directCount; i++) {
			if (_direct[i].width >= atoi(minw))
				_direct[kept++] = _direct[i];
		}
		_directCount = kept;
	}

	// SCUMMVM_DOS_DIRECT_MAXW=<width> the same for modes wider than that: a card
	// with little video memory has no high colour mode of 640x480
	if (const char *maxw = getenv("SCUMMVM_DOS_DIRECT_MAXW")) {
		int kept = 0;
		for (int i = 0; i < _directCount; i++) {
			if (_direct[i].width <= atoi(maxw))
				_direct[kept++] = _direct[i];
		}
		_directCount = kept;
	}

	// Only the modes the monitor and the video memory can take
	int monitorW, monitorH;
	const bool haveMonitor = VesaDisplay::monitorSize(monitorW, monitorH);
	const long memory = (long)VesaDisplay::videoMemoryKB() * 1024;
	Common::String refused;
	int kept = 0;
	for (int i = 0; i < _directCount; i++) {
		const VesaDirectMode &m = _direct[i];
		if (dosModeFits(m.width, m.height, m.pitch, 2, haveMonitor, monitorW, monitorH, memory))
			_direct[kept++] = m;
		else
			refused += Common::String::format(" %dx%dx%d", m.width, m.height, m.bpp);
	}
	_directCount = kept;
	noteRefusedModes(refused, haveMonitor, monitorW, monitorH, memory);

	Common::String list;
	for (int i = 0; i < _directCount && i < 24; i++)
		list += Common::String::format(" %dx%dx%d", _direct[i].width, _direct[i].height, _direct[i].bpp);
	_modeNotes.push_back(Common::String::format("DOSGFX: high colour modes:%s", _directCount ? list.c_str() : " none"));
	logModeNotes();
}

Common::Rect DOSGraphicsManager::gameRectToPhysical(const Common::Rect &r) const {
	return Common::Rect(r.left * _scale + _offX, r.top * _scale + _offY,
	                    r.right * _scale + _offX, r.bottom * _scale + _offY);
}

// A mode of the game's size, else the smallest that holds it doubled, else the smallest
// that holds it. Modes laid out like the game's pixels are preferred.
bool DOSGraphicsManager::pickDirectMode(uint gameW, uint gameH, const Graphics::PixelFormat &format,
                                        VesaDirectMode &best, int &scale) {
	listDirectModes();

	bool found = false;
	for (int pass = 0; pass < 2 && !found; pass++) {
		long bestCost = 0;
		for (int i = 0; i < _directCount; i++) {
			const VesaDirectMode &m = _direct[i];
			if (pass == 0 && !(modeFormat(m) == format))
				continue;
			if (m.width > kGuiMaxWidth || m.height > kGuiMaxHeight)
				continue;

			for (int s = 1; s <= 2; s++) {
				if (m.width < gameW * s || m.height < gameH * s)
					continue;

				long cost;
				if (s == 1 && m.width == gameW && m.height == gameH)
					cost = 0;                                       // exactly the game's size
				else if (s == 2)
					cost = (long)m.width * m.height;                // doubled
				else
					cost = (long)m.width * m.height + 10000000L;    // centred, smaller than doubled

				if (!found || cost < bestCost) {
					found = true;
					bestCost = cost;
					best = m;
					scale = s;
				}
			}
		}
	}
	return found;
}

void DOSGraphicsManager::buildLut(uint16 *&lut, const Graphics::PixelFormat &from, const Graphics::PixelFormat &to) {
	if (!lut)
		lut = new uint16[65536];

	for (uint32 p = 0; p < 65536; p++) {
		uint8 r, g, b;
		from.colorToRGB(p, r, g, b);
		lut[p] = (uint16)to.RGBToColor(r, g, b);
	}
}

bool DOSGraphicsManager::setPhysicalDirect(const VesaDirectMode &m) {
	_banner.clear();
	_bannerBlank = false;
	const Graphics::PixelFormat format = modeFormat(m);
	if (_modeSet && _physBpp == 2 && _physW == m.width && _physH == m.height && _physFormat == format)
		return true;

	// SCUMMVM_DOS_BANKED=1 forces the slower banked access, as for 256 colours
	bool allowLinear = getenv("SCUMMVM_DOS_BANKED") == nullptr;
	_vesa.setPageLimit(pageFlipWanted() ? 2 : 1);
	if (!_vesa.setDirectMode(m.width, m.height, m.bpp, m.redPos, allowLinear)) {
		warning("DOSGFX: could not set the %dx%d %d-bit mode", m.width, m.height, m.bpp);
		return false;
	}

	_useVesa = true;
	_modeSet = true;
	logModeNotes();
	setupPageFlipping();
	_physW = m.width;
	_physH = m.height;
	_physBpp = 2;
	_physFormat = format;
	_scale = 1;
	_dither = false;

	warning("DOSGFX: VESA mode %dx%d, %d-bit colour (%d-%d-%d), %s", _physW, _physH, m.bpp,
	        m.redSize, m.greenSize, m.blueSize, _vesa.isLinear() ? "linear framebuffer" : "banked access");

	// The frame that is sent to video memory: two bytes a pixel
	const size_t need = (size_t)_physW * _physH * 2;
	if (need > _compositeSize) {
		delete[] _composite;
		_compositeSize = need;
		_composite = new byte[_compositeSize];
	}
	memset(_composite, 0, need);

	_overlay.free();
	_overlay.create(_physW, _physH, getOverlayFormat());
	memset(_overlay.getPixels(), 0, _overlay.pitch * _overlay.h);
	if (_physFormat == getOverlayFormat()) {
		delete[] _lutOverlay;
		_lutOverlay = nullptr;
	} else {
		buildLut(_lutOverlay, getOverlayFormat(), _physFormat);
	}

	_hostMouseX = _physW / 2;
	_hostMouseY = _physH / 2;
	dosMouseInit(_physW, _physH);

	_lastCursorRect = Common::Rect();
	markAllDirty();
	_cursorDirty = true;
	_cursorMapDirty = true;
	return true;
}

OSystem::TransactionError DOSGraphicsManager::endDeepTransaction() {
	if (_pendingW == 0 || _pendingH == 0 || _pendingW > kGuiMaxWidth || _pendingH > kGuiMaxHeight)
		return OSystem::kTransactionSizeChangeFailed;

	// SCUMMVM_DOS_DITHER=force shows every high colour game dithered in 256
	// colours, to try that where the card does have the modes
	const char *force = getenv("SCUMMVM_DOS_DITHER");
	const bool forced = force && !strcmp(force, "force");

	VesaDirectMode mode;
	int scale = 1;
	if (forced || !pickDirectMode(_pendingW, _pendingH, _pendingFormat, mode, scale)) {
		if (!forced)
			warning("DOSGFX: no high colour mode holds %ux%u", _pendingW, _pendingH);
		return endDitherTransaction();
	}
	if (!setPhysicalDirect(mode))
		return endDitherTransaction();

	_scale = scale;
	_screen.free();
	_screen.create(_pendingW, _pendingH, _pendingFormat);
	memset(_screen.getPixels(), 0, _screen.pitch * _screen.h);

	_offX = (_physW - (int)_pendingW * scale) / 2;
	_offY = (_physH - (int)_pendingH * scale) / 2;
	if (scale != 1)
		warning("DOSGFX: %ux%u game shown doubled", _pendingW, _pendingH);

	// The game's pixels onto the card's, when they are laid out differently
	if (_pendingFormat == _physFormat) {
		delete[] _lutGame;
		_lutGame = nullptr;
	} else {
		buildLut(_lutGame, _pendingFormat, _physFormat);
	}

	_overlayRaised = false;
	markAllDirty();
	_cursorDirty = true;
	_screenChangeID++;
	return OSystem::kTransactionSuccess;
}

// A high colour game on a 256 colour screen, dithered, when no high colour mode holds it
//
//   dos_16bit_fallback (or SCUMMVM_DOS_DITHER)  false/0 turns it off; "force" uses it always (testing)

bool DOSGraphicsManager::ditherAllowed() const {
	bool allowed = true;
	if (ConfMan.hasKey("dos_16bit_fallback"))
		allowed = ConfMan.getBool("dos_16bit_fallback");
	if (const char *env = getenv("SCUMMVM_DOS_DITHER"))
		allowed = strcmp(env, "force") == 0 || atoi(env) != 0;
	return allowed;
}

void DOSGraphicsManager::buildDitherTables() {
	const Graphics::PixelFormat &f = _screen.format;
	dosDitherChannelTable(_ditherR, f.rBits(), kDitherRed, kDitherMulRed);
	dosDitherChannelTable(_ditherG, f.gBits(), kDitherGreen, kDitherMulGreen);
	dosDitherChannelTable(_ditherB, f.bBits(), kDitherBlue, kDitherMulBlue);

	// For the cursor, which is crisp: its colours arrive as 8-bit components
	dosPlainChannelTable(_plainR, 8, kDitherRed, kDitherMulRed);
	dosPlainChannelTable(_plainG, 8, kDitherGreen, kDitherMulGreen);
	dosPlainChannelTable(_plainB, 8, kDitherBlue, kDitherMulBlue);
}

void DOSGraphicsManager::writeDitherDAC() {
	uint8 cube[256][3];
	dosDitherPalette(cube);
	writeDAC(&cube[0][0], 0, 256);
}

OSystem::TransactionError DOSGraphicsManager::endDitherTransaction() {
	if (!ditherAllowed()) {
		warning("DOSGFX: high colour is not possible here and the 256 colour fallback is switched off");
		return OSystem::kTransactionSizeChangeFailed;
	}

	// The 256 colour mode: VGA 13h for a game of up to 320x200, otherwise the
	// smallest VESA mode that holds the game (the game is centred in it)
	int modeW, modeH;
	if (_pendingW <= (uint)kVgaWidth && _pendingH <= (uint)kVgaHeight) {
		modeW = kVgaWidth;
		modeH = kVgaHeight;
	} else {
		DosModeSize modes[64];
		int count = listFittingModes(modes, 64);
		bool found = false;
		modeW = modeH = 0;
		for (int i = 0; i < count; i++) {
			if (modes[i].w < (int)_pendingW || modes[i].h < (int)_pendingH)
				continue;
			if (modes[i].w > kGuiMaxWidth || modes[i].h > kGuiMaxHeight)
				continue;
			if (!found || (long)modes[i].w * modes[i].h < (long)modeW * modeH) {
				modeW = modes[i].w;
				modeH = modes[i].h;
				found = true;
			}
		}
		if (!found) {
			warning("DOSGFX: no 256 colour mode holds %ux%u either", _pendingW, _pendingH);
			return OSystem::kTransactionSizeChangeFailed;
		}
	}

	if (!setPhysicalMode(modeW, modeH))
		return OSystem::kTransactionSizeChangeFailed;

	_screen.free();
	_screen.create(_pendingW, _pendingH, _pendingFormat);
	memset(_screen.getPixels(), 0, _screen.pitch * _screen.h);
	_offX = (_physW - (int)_pendingW) / 2;
	_offY = (_physH - (int)_pendingH) / 2;

	_dither = true;
	buildDitherTables();
	_dacKnown = false;
	writeDitherDAC();
	warning("DOSGFX: %ux%u high colour game shown in 256 colours (dithered) in a %dx%d mode", _pendingW, _pendingH, _physW, _physH);

	_overlayRaised = false;
	markAllDirty();
	_cursorDirty = true;
	_cursorMapDirty = true;
	_screenChangeID++;
	return OSystem::kTransactionSuccess;
}

// The game pixels in r as palette indices, through the per-channel dither tables
void DOSGraphicsManager::renderRectDither(byte *dst, const Common::Rect &r) {
	const Graphics::PixelFormat &f = _screen.format;
	const int rs = f.rShift, gs = f.gShift, bs = f.bShift;
	const unsigned rm = (1u << f.rBits()) - 1, gm = (1u << f.gBits()) - 1, bm = (1u << f.bBits()) - 1;
	const bool haveGame = _screen.getPixels() && _screen.format.bytesPerPixel == 2;

	for (int y = r.top; y < r.bottom; y++) {
		byte *d = dst + y * _physW;
		const int gy = y - _offY;

		// Outside the game screen: black (palette entry 0)
		if (!haveGame || gy < 0 || gy >= _screen.h) {
			memset(d + r.left, 0, r.width());
			continue;
		}

		const int gx0 = MAX<int>(r.left, _offX);
		const int gx1 = MIN<int>(r.right, _offX + _screen.w);
		if (gx0 >= gx1) {
			memset(d + r.left, 0, r.width());
			continue;
		}

		if (gx0 > r.left)
			memset(d + r.left, 0, gx0 - r.left);

		// The matrix row of this screen line: four entries of 256 table slots each
		const uint8 *rt = _ditherR + (y & 3) * 4 * 256;
		const uint8 *gt = _ditherG + (y & 3) * 4 * 256;
		const uint8 *bt = _ditherB + (y & 3) * 4 * 256;

		const uint16 *s = (const uint16 *)_screen.getBasePtr(gx0 - _offX, gy);

		// The unrolled conversion of the line (dos-dither.h; the unit tests check that it
		// gives the same bytes as doing the pixels one at a time)
		dosDitherRow(d, s, gx0, gx1, rt, gt, bt, rs, gs, bs, rm, gm, bm);

		if (gx1 < r.right)
			memset(d + gx1, 0, r.right - gx1);
	}
}

// A high colour cursor on the 256 colour screen: nearest colour, no pattern
void DOSGraphicsManager::drawCursorDithered(byte *dst, const Common::Rect &clip) {
	const int ox = _hostMouseX - _hotX;
	const int oy = _hostMouseY - _hotY;

	const int y0 = MAX<int>(clip.top, oy);
	const int y1 = MIN<int>(clip.bottom, oy + (int)_cursorH);
	const int x0 = MAX<int>(clip.left, ox);
	const int x1 = MIN<int>(clip.right, ox + (int)_cursorW);

	for (int y = y0; y < y1; y++) {
		const uint16 *src = (const uint16 *)_cursor + (y - oy) * _cursorW + (x0 - ox);
		byte *d = dst + y * _physW;
		for (int x = x0; x < x1; x++, src++) {
			if ((uint32) * src == _keycolor)
				continue;
			uint8 r, g, b;
			_cursorFormat.colorToRGB(*src, r, g, b);
			d[x] = _plainR[r] + _plainG[g] + _plainB[b];
		}
	}
}

// Whether the menus get a high colour mode, once their size is known
void DOSGraphicsManager::chooseGuiDepth() {
	_guiBpp = 8;

	Common::String depth("auto");
	if (ConfMan.hasKey("dos_gui_depth"))
		depth = ConfMan.get("dos_gui_depth");
	if (const char *env = getenv("SCUMMVM_DOS_GUI_DEPTH"))
		depth = env;
	if (depth == "8")
		return;

	listDirectModes();

	// The same size as the 256 colour menu; RGB565 ahead of 555
	int pick = -1;
	for (int i = 0; i < _directCount; i++) {
		if (_direct[i].width == _guiW && _direct[i].height == _guiH) {
			if (pick < 0 || (_direct[i].greenSize == 6 && _direct[pick].greenSize != 6))
				pick = i;
		}
	}
	if (pick < 0) {
		if (depth == "16")
			_modeNotes.push_back(Common::String::format("DOSGFX: no high colour mode of %dx%d; menus use 256 colours", _guiW, _guiH));
		return;
	}

	_guiBpp = 16;
	_guiRedPos = _direct[pick].redPos;
}

/** Render the game screen, or the overlay, for the pixels in r, at two bytes a pixel. */
void DOSGraphicsManager::renderRect16(byte *dst, const Common::Rect &r) {
	const size_t rowBytes = (size_t)_physW * 2;

	if (_overlayVisible) {
		for (int y = r.top; y < r.bottom; y++) {
			const uint16 *src = (const uint16 *)_overlay.getBasePtr(0, y);
			uint16 *d = (uint16 *)(dst + y * rowBytes);
			if (!_lutOverlay) {
				memcpy(d + r.left, src + r.left, (size_t)r.width() * 2);
			} else {
				for (int x = r.left; x < r.right; x++)
					d[x] = _lutOverlay[src[x]];
			}
		}
		return;
	}

	// A 256 colour game screen cannot be shown on a high colour one (it is only
	// there between a menu and the game that replaces it): black
	const bool haveGame = _screen.getPixels() && _screen.format.bytesPerPixel == 2;
	const int gameW = haveGame ? _screen.w * _scale : 0;
	const int gameH = haveGame ? _screen.h * _scale : 0;

	for (int y = r.top; y < r.bottom; y++) {
		uint16 *d = (uint16 *)(dst + y * rowBytes);
		const int gy = y - _offY;

		// Outside the game screen: black
		if (gy < 0 || gy >= gameH) {
			memset(d + r.left, 0, (size_t)r.width() * 2);
			continue;
		}

		const int gx0 = MAX<int>(r.left, _offX);
		const int gx1 = MIN<int>(r.right, _offX + gameW);
		if (gx0 >= gx1) {
			memset(d + r.left, 0, (size_t)r.width() * 2);
			continue;
		}

		if (gx0 > r.left)
			memset(d + r.left, 0, (size_t)(gx0 - r.left) * 2);

		const uint16 *src = (const uint16 *)_screen.getBasePtr(0, gy / _scale);
		if (_scale == 1) {
			const uint16 *s = src + (gx0 - _offX);
			if (!_lutGame) {
				memcpy(d + gx0, s, (size_t)(gx1 - gx0) * 2);
			} else {
				for (int x = gx0; x < gx1; x++)
					d[x] = _lutGame[s[x - gx0]];
			}
		} else {
			for (int x = gx0; x < gx1; x++) {
				uint16 p = src[(x - _offX) >> 1];
				d[x] = _lutGame ? _lutGame[p] : p;
			}
		}

		if (gx1 < r.right)
			memset(d + gx1, 0, (size_t)(r.right - gx1) * 2);
	}
}

void DOSGraphicsManager::rebuildCursorMap16() {
	_cursorMapDirty = false;

	// The colours of a palettised cursor, as pixels of the card's format
	const byte *pal = _cursorPaletteEnabled ? _cursorPalette : _palette;
	for (int i = 0; i < 256; i++)
		_cursorMap16[i] = (uint16)_physFormat.RGBToColor(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]);
}

/** Draw the cursor into a high colour frame, but only the part inside clip. */
void DOSGraphicsManager::drawCursor16(byte *dst, const Common::Rect &clip) {
	if (_cursorMapDirty)
		rebuildCursorMap16();

	const size_t rowBytes = (size_t)_physW * 2;
	const int ox = _hostMouseX - _hotX;
	const int oy = _hostMouseY - _hotY;

	const int y0 = MAX<int>(clip.top, oy);
	const int y1 = MIN<int>(clip.bottom, oy + (int)_cursorH);
	const int x0 = MAX<int>(clip.left, ox);
	const int x1 = MIN<int>(clip.right, ox + (int)_cursorW);

	for (int y = y0; y < y1; y++) {
		uint16 *d = (uint16 *)(dst + y * rowBytes);

		if (_cursorBpp == 1) {
			const byte *src = _cursor + (y - oy) * _cursorW + (x0 - ox);
			for (int x = x0; x < x1; x++, src++) {
				if ((uint32) * src == _keycolor)
					continue;
				d[x] = _cursorMap16[*src];
			}
		} else {
			const uint16 *src = (const uint16 *)_cursor + (y - oy) * _cursorW + (x0 - ox);
			const bool same = _cursorFormat == _physFormat;
			for (int x = x0; x < x1; x++, src++) {
				if ((uint32) * src == _keycolor)
					continue;
				if (same) {
					d[x] = *src;
				} else {
					uint8 r, g, b;
					_cursorFormat.colorToRGB(*src, r, g, b);
					d[x] = (uint16)_physFormat.RGBToColor(r, g, b);
				}
			}
		}
	}
}

// Redraw everything from scratch and compare with what is in video memory.
void DOSGraphicsManager::verifyFrame16() {
	const Common::Rect full(0, 0, _physW, _physH);
	const size_t bytes = (size_t)_physW * _physH * 2;

	renderRect(_verifyBuf, full);
	drawCursor(_verifyBuf, full);
	_vesa.readAll16(_vramBuf);

	_verifyFrames++;

	if (memcmp(_verifyBuf, _vramBuf, bytes) != 0) {
		_verifyBad++;
		if (_verifyBad <= 5) {
			const uint16 *a = (const uint16 *)_verifyBuf;
			const uint16 *b = (const uint16 *)_vramBuf;
			int first = 0, count = 0;
			for (int p = 0; p < _physW * _physH; p++) {
				if (a[p] != b[p]) {
					if (!count)
						first = p;
					count++;
				}
			}
			warning("DOSGFX verify: frame %u differs in %d pixels, first at x=%d y=%d (expected %04x, video memory has %04x)",
			        _verifyFrames, count, first % _physW, first / _physW, a[first], b[first]);
		}
	}

	if (_verifyFrames == 20 || _verifyFrames == 200 || _verifyFrames == 1000 || _verifyFrames % 5000 == 0)
		warning("DOSGFX verify: %u frames checked, %u differed (%dx%d, %d%s%s)", _verifyFrames, _verifyBad,
		        _physW, _physH, _physFormat.bytesPerPixel * 8, _vesa.isLinear() ? ", VESA linear" : ", VESA banked",
		        _overlayVisible ? ", overlay" : "");
}

void DOSGraphicsManager::selfTest16() {
	warning("DOSGFX selftest: start");
	static const int sizes[][2] = { { 320, 200 }, { 640, 480 } };

	Common::List<Graphics::PixelFormat> formats = getSupportedFormats();
	for (const auto &fmt : formats) {
		if (fmt.bytesPerPixel != 2)
			continue;

		for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
			const int w = sizes[s][0], h = sizes[s][1];

			beginGFXTransaction();
			initSize(w, h, &fmt);
			OSystem::TransactionError err = endGFXTransaction();
			if (err != OSystem::kTransactionSuccess) {
				warning("DOSGFX selftest: %dx%d, red at bit %d: refused (%d)", w, h, fmt.rShift, (int)err);
				continue;
			}
			warning("DOSGFX selftest: %dx%d, red at bit %d: accepted, screen format %d bytes, scale %d, offset %d,%d",
			        w, h, fmt.rShift, (int)getScreenFormat().bytesPerPixel, _scale, _offX, _offY);

			// Red grows to the right, green downwards, blue is half; a white frame
			Graphics::Surface pat;
			pat.create(w, h, fmt);
			for (int y = 0; y < h; y++) {
				uint16 *row = (uint16 *)pat.getBasePtr(0, y);
				for (int x = 0; x < w; x++) {
					uint8 r = x * 255 / (w - 1), g = y * 255 / (h - 1), b = 128;
					if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2)
						r = g = b = 255;
					row[x] = (uint16)fmt.RGBToColor(r, g, b);
				}
			}
			copyRectToScreen(pat.getPixels(), pat.pitch, 0, 0, w, h);
			pat.free();

			// An 8x8 cursor of the game's format: white, with a magenta corner that is see-through
			const uint32 key = fmt.RGBToColor(255, 0, 255);
			uint16 cur[64];
			for (int i = 0; i < 64; i++)
				cur[i] = (uint16)fmt.RGBToColor(255, 255, 255);
			for (int i = 0; i < 4; i++)
				cur[i] = (uint16)key;
			setMouseCursor(cur, 8, 8, 0, 0, key, &fmt, nullptr, 0, 0);
			showMouse(true);
			warpMouse(w / 2, h / 2);

			updateScreen();
			saveScreenshot();
		}
	}

	// And back to 256 colours, as when a high colour game ends
	beginGFXTransaction();
	initSize(320, 200);
	OSystem::TransactionError err = endGFXTransaction();
	warning("DOSGFX selftest: back to 256 colours: %s, physical %dx%d, %d byte(s) a pixel", err == OSystem::kTransactionSuccess ? "ok" : "FAILED",
	        _physW, _physH, _physBpp);
	updateScreen();
	warning("DOSGFX selftest: done");
}

// A 24-bit BMP of what the screen shows, the cursor included
void DOSGraphicsManager::saveScreenshot16() {
	char name[16];
	_shotCount++;
	snprintf(name, sizeof(name), "shot%04u.bmp", _shotCount);

	FILE *f = fopen(name, "wb");
	if (!f)
		return;

	const int w = _physW, h = _physH;
	const int rowBytes = (w * 3 + 3) & ~3;
	const unsigned dataOffset = 14 + 40;

	uint8 header[14 + 40];
	memset(header, 0, sizeof(header));
	header[0] = 'B';
	header[1] = 'M';
	WRITE_LE_UINT32(header + 2, dataOffset + rowBytes * h);
	WRITE_LE_UINT32(header + 10, dataOffset);
	WRITE_LE_UINT32(header + 14, 40);
	WRITE_LE_UINT32(header + 18, w);
	WRITE_LE_UINT32(header + 22, h);
	WRITE_LE_UINT16(header + 26, 1);
	WRITE_LE_UINT16(header + 28, 24);
	WRITE_LE_UINT32(header + 34, rowBytes * h);
	fwrite(header, 1, sizeof(header), f);

	uint8 *row = new uint8[rowBytes];
	for (int y = h - 1; y >= 0; y--) {
		memset(row, 0, rowBytes);
		const uint16 *s = (const uint16 *)(_composite + (size_t)y * w * 2);
		for (int x = 0; x < w; x++) {
			uint8 r, g, b;
			_physFormat.colorToRGB(s[x], r, g, b);
			row[x * 3 + 0] = b;
			row[x * 3 + 1] = g;
			row[x * 3 + 2] = r;
		}
		fwrite(row, 1, rowBytes, f);
	}
	delete[] row;
	fclose(f);
}

#endif // USE_RGB_COLOR
