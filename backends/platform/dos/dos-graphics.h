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

#ifndef BACKENDS_PLATFORM_DOS_GRAPHICS_H
#define BACKENDS_PLATFORM_DOS_GRAPHICS_H

#include "backends/graphics/graphics.h"
#include "backends/platform/dos/dos-banner.h"
#include "backends/platform/dos/dos-dither.h"
#include "backends/platform/dos/dos-vesa.h"
#include "common/rect.h"
#include "common/str-array.h"
#include "graphics/surface.h"

/**
 * Graphics through VGA mode 13h (up to 320x200) and VESA modes (larger games,
 * the menus, and high colour).
 *
 * A smaller game screen is centred. On a 256 colour screen the RGB565 overlay
 * is shown through a fixed 3-3-2 palette. The cursor is drawn in software, and
 * only what changed is sent to video memory.
 *
 * SCUMMVM_DOS_VERIFY=1 checks video memory against a full redraw after every update.
 */
class DOSGraphicsManager : public GraphicsManager {
public:
	enum {
		kVgaWidth = 320,     ///< Physical screen of VGA mode 13h
		kVgaHeight = 200,
		kMaxWidth = 640,     ///< The largest physical screen a game gets
		kMaxHeight = 480,
		kGuiMaxWidth = 1600, ///< The largest the menus may be
		kGuiMaxHeight = 1200
	};

	DOSGraphicsManager();
	~DOSGraphicsManager() override;

	// Features

	bool hasFeature(OSystem::Feature f) const override;
	void setFeatureState(OSystem::Feature f, bool enable) override;
	bool getFeatureState(OSystem::Feature f) const override;

	#ifdef USE_RGB_COLOR
	Graphics::PixelFormat getScreenFormat() const override;
	Common::List<Graphics::PixelFormat> getSupportedFormats() const override;
	#endif

	// Screen size / transactions

	void initSize(uint width, uint height, const Graphics::PixelFormat *format = NULL) override;
	int getScreenChangeID() const override {
		return _screenChangeID;
	}
	void beginGFXTransaction() override;
	OSystem::TransactionError endGFXTransaction() override;
	int16 getHeight() const override;
	int16 getWidth() const override;

	// Palette

	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;

	// Drawing

	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	Graphics::Surface *lockScreen() override;
	void unlockScreen() override;
	void fillScreen(uint32 col) override;
	void fillScreen(const Common::Rect &r, uint32 col) override;
	void updateScreen() override;
	void setShakePos(int shakeXOffset, int shakeYOffset) override {}
	void setFocusRectangle(const Common::Rect &rect) override {}
	void clearFocusRectangle() override {}

	// Overlay

	void showOverlay(bool inGUI) override;
	void hideOverlay() override;
	bool isOverlayVisible() const override {
		return _overlayVisible;
	}
	Graphics::PixelFormat getOverlayFormat() const override;
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;
	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
	int16 getOverlayHeight() const override {
		return _physH;
	}
	int16 getOverlayWidth() const override {
		return _physW;
	}

	// Mouse cursor

	bool showMouse(bool visible) override;
	void warpMouse(int x, int y) override;
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor, const Graphics::PixelFormat *format, const byte *mask, frac_t scaleX, frac_t scaleY) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override;

	// DOS specific

	/** True once a graphics mode has been set. */
	bool isGraphicsMode() const {
		return _modeSet;
	}

	/** Show a line of text at the top of the screen, until something is drawn. */
	void showBanner(const char *text);

	/** Return to 80x25 text mode. Safe to call more than once. */
	void restoreTextMode();

	/** Mouse position in physical (320x200) screen coordinates. */
	void setHostMouse(int x, int y);

	/** Mouse position in game (or overlay) coordinates. */
	void getGameMouse(int &x, int &y) const;

	/**
	 * Save the screen as SHOT0001.BMP, SHOT0002.BMP... in the current directory
	 * (also every so many seconds with SCUMMVM_DOS_SHOTS=<seconds>).
	 */
	void saveScreenshot() override;

	#ifdef USE_RGB_COLOR
	/** Testing aid (SCUMMVM_DOS_SELFTEST16=1): a screenshot in each high colour format */
	void selfTest16();
	#endif

private:
	enum { kMaxDirty = 16 };

	void ensureVideoMode();
	void drawGameIntoOverlay();
	void waitRetrace();
	void drawBanner(byte *dst, const Common::Rect &clip);
	void clearBanner();
	Common::Rect bannerRect() const;
	bool loadRomFont();
	bool pageFlipWanted() const;
	void setupPageFlipping();
	void applyPalette();
	void presentDirect(bool big, bool blank);
	void presentFlipped();
	bool setPhysicalMode(int width, int height);

	/** True while no game is running: the launcher and its dialogs. */
	static bool isLauncherContext();

	/** Work out (once) the resolution the menus should use; false means 320x200. */
	bool chooseGuiMode(int &width, int &height);

	/** Switch to the menus' resolution, or 320x200 if that fails. */
	bool setLauncherMode();

	#ifdef USE_RGB_COLOR
	// High colour (15 and 16 bit) screens

	/** Ask the card, once, which high colour modes it has. */
	void listDirectModes();

	/** Decide whether the menus use a high colour mode of the size already chosen. */
	void chooseGuiDepth();
	static Graphics::PixelFormat modeFormat(const VesaDirectMode &m);

	/** The card's mode for a game of this size and format (doubled when small), or false. */
	bool pickDirectMode(uint gameW, uint gameH, const Graphics::PixelFormat &format, VesaDirectMode &mode, int &scale);
	bool setPhysicalDirect(const VesaDirectMode &m);
	OSystem::TransactionError endDeepTransaction();

	/** A rectangle of the game screen, as it is on the physical screen. */
	Common::Rect gameRectToPhysical(const Common::Rect &r) const;

	static void buildLut(uint16 *&lut, const Graphics::PixelFormat &from, const Graphics::PixelFormat &to);
	void renderRect16(byte *dst, const Common::Rect &r);
	void rebuildCursorMap16();
	void drawCursor16(byte *dst, const Common::Rect &clip);
	void verifyFrame16();
	void saveScreenshot16();

	// A high colour game on a 256 colour screen (see dos-dither.h)

	bool ditherAllowed() const;
	OSystem::TransactionError endDitherTransaction();
	void buildDitherTables();
	void writeDitherDAC();
	void renderRectDither(byte *dst, const Common::Rect &r);
	void drawCursorDithered(byte *dst, const Common::Rect &clip);
	#endif
	void writeDAC(const byte *rgb8, uint start, uint num);
	void writeOverlayDAC();
	void rebuildCursorMap();

	// Dirty rectangles (physical 320x200 coordinates)

	void addDirty(const Common::Rect &r);
	void markAllDirty() {
		_dirtyAll = true;
	}
	Common::Rect cursorRect() const;

	/** Render the game screen (or overlay) into dst, a 320x200 buffer. */
	void renderRect(byte *dst, const Common::Rect &r);
	void drawCursor(byte *dst, const Common::Rect &clip);
	void blitRect(const Common::Rect &r);
	void verifyFrame();

	bool _modeSet;
	int _screenChangeID;

	// The physical screen: 320x200 (VGA mode 13h) or a VESA mode

	int _physW, _physH;
	VesaDisplay _vesa;
	bool _useVesa;

	// Game screen

	Graphics::Surface _screen;
	int _offX, _offY;

	// Pending transaction

	bool _pending;
	uint _pendingW, _pendingH;
	bool _pendingFormatOk;

	// Palettes (8 bits per component)

	byte _palette[256 * 3];
	byte _cursorPalette[256 * 3];
	bool _cursorPaletteEnabled;

	// Overlay

	Graphics::Surface _overlay;
	bool _overlayVisible;
	bool _overlayRaised; ///< a 320x200 game's overlay is shown in 640x480 and the game's mode comes back

	// Cursor

	byte *_cursor;
	uint _cursorW, _cursorH;
	int _hotX, _hotY;
	uint32 _keycolor;
	bool _cursorVisible;
	byte _cursorMap[256];
	bool _cursorMapDirty;
	int _hostMouseX, _hostMouseY;

	// What is in video memory (the game or overlay with the cursor on top)

	byte *_composite;
	size_t _compositeSize;

	// The menus' resolution (see chooseGuiMode())

	bool _guiModeChosen;
	bool _guiModeValid;
	int _guiW, _guiH;

	/** What choosing a mode found, warned once the screen is in a graphics mode (logModeNotes()) */
	Common::StringArray _modeNotes;
	void logModeNotes();

	#ifdef USE_RGB_COLOR
	// High colour

	int _physBpp;                       ///< Bytes per physical pixel: 1, or 2 in high colour
	Graphics::PixelFormat _physFormat;  ///< Its pixel format when it is high colour
	int _scale;                         ///< 1, or 2 when a small high colour game is shown doubled
	VesaDirectMode _direct[32];         ///< The card's high colour modes
	int _directCount;
	bool _directListed;
	Graphics::PixelFormat _pendingFormat;
	uint16 *_lutGame;                   ///< game pixel -> physical pixel, only when their formats differ
	uint16 *_lutOverlay;                ///< RGB565 overlay pixel -> physical pixel, unless the screen is RGB565
	uint16 _cursorMap16[256];           ///< cursor palette index -> physical pixel
	uint _cursorBpp;                    ///< Bytes per pixel of the stored cursor image
	Graphics::PixelFormat _cursorFormat;
	int _guiBpp;                        ///< Bits per pixel chosen for the menus: 8 or 16
	int _guiRedPos;                     ///< When 16: the red field of that mode (11 or 10)

	// A high colour game shown on a 256 colour screen, dithered

	bool _dither;
	uint8 _ditherR[16 * 256];           ///< per matrix entry and raw channel value: palette index share
	uint8 _ditherG[16 * 256];
	uint8 _ditherB[16 * 256];
	uint8 _plainR[256];                 ///< the same without the pattern (for the cursor)
	uint8 _plainG[256];
	uint8 _plainB[256];
	#endif

	// What has changed since the last updateScreen()

	Common::Rect _dirty[kMaxDirty];
	int _dirtyCount;
	bool _dirtyAll;
	bool _palettePending; ///< game palette changed, DAC not written yet
	Common::String _banner;      ///< lines, each ended by a newline
	DosBannerLayout _bannerLayout; ///< for the screen the banner was laid out on
	bool _bannerBlank;           ///< shown before the game started: the rest of the screen is black
	uint8 _romFont[256 * 8];
	bool _romFontLoaded;
	bool _flipping;       ///< two display pages: draw to the hidden one, then switch
	Common::Rect _prevBox; ///< what the last flip left stale on the page being drawn to
	uint64 _lastFlip;
	uint64 _bigBlitTicks; ///< how long the last redraw of most of the screen took (0: not known yet)
	bool _dacKnown;       ///< the DAC holds the game palette (not black, the overlay's or the BIOS default)
	Common::Rect _lastCursorRect;
	bool _cursorDirty;

	// Self-check (SCUMMVM_DOS_VERIFY)

	bool _verify;
	byte *_verifyBuf;
	byte *_vramBuf;
	unsigned _verifyFrames, _verifyBad;

	// Periodic screenshots (SCUMMVM_DOS_SHOTS)

	uint64 _shotInterval;       ///< in uclock() ticks, 0 = off
	uint64 _shotNext;
	unsigned _shotCount;
};

#endif
