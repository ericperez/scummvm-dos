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

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "common/scummsys.h"

#if defined(USE_DOS_SDL)

#include <dpmi.h>
#include <go32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>
#include <time.h>
#include <unistd.h>

#include "backends/fs/posix/posix-fs-factory.h"
#include "backends/events/sdl/sdl-events.h"
#include "backends/graphics/sdl/sdl-graphics.h"
#include "backends/platform/dos/dos-banner.h"
#include "backends/platform/dos/dos-cdaudio.h"
#include "backends/platform/dos/dos-files.h"
#include "backends/platform/dos/dos-modes.h"
#include "backends/platform/dos/dos-net.h"
#include "backends/platform/dos/dos-openmpt.h"
#include "backends/platform/dos/dos-sdl.h"
#include "backends/platform/dos/dos-vesa.h"
#include "backends/platform/sdl/sdl-sys.h"
#include "backends/saves/default/default-saves.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "engines/engine.h"
#include "gui/gui-manager.h"

void dosOplSilence();

/**
 * SDL's DOS sound thread is cooperative and makes one device buffer (~23 ms) per turn, so a slow
 * event loop starves the card: give it several turns per poll (cheap once the ring is full).
 */
class DOSAudioPumpEventSource : public Common::EventSource {
public:
	DOSAudioPumpEventSource(Common::EventSource *source) : _source(source) {}

	bool pollEvent(Common::Event &event) override {
		for (int i = 0; i < kAudioTurns; i++)
			SDL_Delay(0);
		return _source->pollEvent(event);
	}

	bool allowMapping() const override {
		return _source->allowMapping();
	}

private:
	// SDL's ring holds four device buffers; one more turn comes with the poll's own pump
	enum { kAudioTurns = 3 };

	Common::EventSource *_source;
};

/**
 * Ask for an RGB565 video mode of at least this size, rather than the 32-bit one SDL's DOS driver would take.
 *
 * @param modeWidth, modeHeight  the size of the mode chosen, 0 if there is none
 * @return true if a mode was asked for (SurfaceSdlGraphicsManager::SDL_SetVideoMode() checks it took)
 */
bool dosChooseVideoMode(SDL_Window *window, int width, int height, int &modeWidth, int &modeHeight) {
	modeWidth = modeHeight = 0;

	int count = 0;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(window), &count);
	const SDL_DisplayMode *best = nullptr;
	for (int i = 0; modes && i < count; i++) {
		const SDL_DisplayMode *mode = modes[i];
		if (mode->format != SDL_PIXELFORMAT_RGB565 || mode->w < width || mode->h < height)
			continue;
		if (!best || mode->w * mode->h < best->w * best->h)
			best = mode;
	}

	bool chosenOk = false;
	if (best) {
		const SDL_DisplayMode chosen = *best;
		// The driver sets the video mode only for a fullscreen window
		if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) && (!SDL_SetWindowFullscreen(window, true) || !SDL_SyncWindow(window)))
			warning("DOS: could not make the window fullscreen: %s", SDL_GetError());
		if (!SDL_SetWindowFullscreenMode(window, &chosen) || !SDL_SyncWindow(window)) {
			warning("DOS: could not select the %dx%d RGB565 video mode: %s", chosen.w, chosen.h, SDL_GetError());
		} else {
			debug(1, "DOS: video mode %dx%d, RGB565", chosen.w, chosen.h);
			modeWidth = chosen.w;
			modeHeight = chosen.h;
			chosenOk = true;
		}
	}
	SDL_free(modes);
	return chosenOk;
}

// The launcher's resolution, once chosen (dosGuiResolution())
static bool g_guiChosen = false, g_guiValid = false;
static uint g_guiW = 0, g_guiH = 0;

/**
 * The launcher's resolution, picked as DOSGraphicsManager::chooseGuiMode() does (same settings), from the
 * RGB565 modes. Decided once.
 *
 * @return false to keep the launcher at 320x200 (shown at 640x480).
 */
bool dosGuiResolution(uint &width, uint &height) {
	if (g_guiChosen) {
		width = g_guiW;
		height = g_guiH;
		return g_guiValid;
	}
	g_guiChosen = true;

	Common::String setting("auto");
	if (ConfMan.hasKey("dos_gui_mode"))
		setting = ConfMan.get("dos_gui_mode");
	if (const char *env = getenv("SCUMMVM_DOS_GUI_MODE"))
		setting = env;
	setting.toLowercase();

	if (setting == "320x200" || setting == "vga" || setting == "off" || setting == "small")
		return false;

	int explicitW = 0, explicitH = 0;
	const bool wantExplicit = setting != "auto" && dosParseSize(setting.c_str(), explicitW, explicitH);
	if (setting != "auto" && !wantExplicit)
		warning("DOSGFX: dos_gui_mode '%s' is not 'auto' or a size like 800x600; using auto", setting.c_str());

	int maxW = 1024, maxH = 768;
	Common::String maxSetting;
	if (ConfMan.hasKey("dos_gui_max"))
		maxSetting = ConfMan.get("dos_gui_max");
	if (const char *env = getenv("SCUMMVM_DOS_GUI_MAX"))
		maxSetting = env;
	if (!maxSetting.empty() && !dosParseSize(maxSetting.c_str(), maxW, maxH)) {
		warning("DOSGFX: dos_gui_max '%s' is not a size like 1024x768; using 1024x768", maxSetting.c_str());
		maxW = 1024;
		maxH = 768;
	}

	// The sizes the display has in RGB565, each once
	DosModeSize sizes[64];
	int count = 0;
	int modeCount = 0;
	SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &modeCount);
	for (int i = 0; modes && i < modeCount && count < 64; i++) {
		if (modes[i]->format != SDL_PIXELFORMAT_RGB565)
			continue;
		bool known = false;
		for (int j = 0; j < count; j++)
			known = known || (sizes[j].w == modes[i]->w && sizes[j].h == modes[i]->h);
		if (!known) {
			sizes[count].w = modes[i]->w;
			sizes[count].h = modes[i]->h;
			count++;
		}
	}
	SDL_free(modes);

	if (wantExplicit) {
		for (int i = 0; i < count; i++) {
			if (sizes[i].w == explicitW && sizes[i].h == explicitH) {
				g_guiW = explicitW;
				g_guiH = explicitH;
				g_guiValid = true;
				width = g_guiW;
				height = g_guiH;
				return true;
			}
		}
		warning("DOSGFX: no %dx%d mode; choosing one", explicitW, explicitH);
	}

	// What the monitor says it is
	bool haveNative = false;
	int nativeW = 0, nativeH = 0;
	bool askMonitor = true;
	if (ConfMan.hasKey("dos_gui_edid"))
		askMonitor = ConfMan.getBool("dos_gui_edid");
	if (const char *env = getenv("SCUMMVM_DOS_EDID"))
		askMonitor = askMonitor && atoi(env) != 0;

	const char *fake = getenv("SCUMMVM_DOS_EDID_SIZE");
	if (fake && dosParseSize(fake, nativeW, nativeH)) {
		haveNative = true;
	} else if (askMonitor) {
		uint8 edid[128];
		haveNative = VesaDisplay::readEdid(edid) && dosEdidSize(edid, nativeW, nativeH);
		if (haveNative)
			warning("DOSGFX: the monitor reports %dx%d", nativeW, nativeH);
	}

	DosModeSize best;
	if (!dosChooseGuiMode(sizes, count, maxW, maxH, haveNative, nativeW, nativeH, best)) {
		warning("DOSGFX: no RGB565 mode suits (limit %dx%d); the menus stay at 640x480", maxW, maxH);
		return false;
	}

	g_guiW = best.w;
	g_guiH = best.h;
	g_guiValid = true;
	warning("DOSGFX: menus use %dx%d (limit %dx%d)", g_guiW, g_guiH, maxW, maxH);
	width = g_guiW;
	height = g_guiH;
	return true;
}

/** The start-up banner, as in the dos backend: green text in the card's BIOS font until the game draws */
static char g_banner[256];       ///< the text as logged
static bool g_bannerBlank = false; ///< shown before the game started: the rest of the screen is black
static uint8 g_romFont[256 * 8];
static bool g_romFontLoaded = false;

static bool loadRomFont() {
	if (g_romFontLoaded)
		return true;

	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	r.x.ax = 0x1130; // get font information
	r.h.bh = 3;      // the ROM 8x8 font
	__dpmi_int(0x10, &r);
	const unsigned long address = ((unsigned long)r.x.es << 4) + r.x.bp;
	if (!address)
		return false;

	dosmemget(address, sizeof(g_romFont), g_romFont);
	g_romFontLoaded = true;
	return true;
}

// Draw the banner, if there is one, into the window's surface. Returns true if it is there to be shown.
bool dosBannerDraw(SDL_Surface *surface) {
	if (!g_banner[0] || !surface || !loadRomFont())
		return false;
	const DosBannerLayout layout = dosBannerLayout(surface->w, surface->h);
	char lines[sizeof(g_banner) * 2];
	dosBannerWrap(g_banner, layout.perLine, layout.maxLines, lines, sizeof(lines));

	const int bpp = SDL_BYTESPERPIXEL(surface->format);
	if (bpp != 2 && bpp != 4)
		return false;
	const Uint32 color = SDL_MapSurfaceRGB(surface, 0, 255, 0);
	if (g_bannerBlank)
		SDL_FillSurfaceRect(surface, nullptr, 0);

	int x = layout.margin, y = layout.margin;
	for (uint i = 0; lines[i]; i++) {
		const char c = lines[i];
		if (c == '\n') {
			x = layout.margin;
			y += layout.lineStep;
			continue;
		}

		const uint8 *glyph = g_romFont + (uint8)c * 8;
		for (int row = 0; row < 8; row++) {
			for (int col = 0; col < 8; col++) {
				if (!(glyph[row] & (0x80 >> col)))
					continue;
				for (int dy = 0; dy < layout.scale; dy++) {
					const int py = y + row * layout.scale + dy;
					if (py < 0 || py >= surface->h)
						continue;
					for (int dx = 0; dx < layout.scale; dx++) {
						const int px = x + col * layout.scale + dx;
						if (px < 0 || px >= surface->w)
							continue;
						Uint8 *q = (Uint8 *)surface->pixels + py * surface->pitch + px * bpp;
						if (bpp == 2)
							*(Uint16 *)q = (Uint16)color;
						else
							*(Uint32 *)q = color;
					}
				}
			}
		}
		x += layout.glyph;
	}
	return true;
}

// The game draws: the banner is done with. Returns true if there was one.
bool dosBannerClear() {
	if (!g_banner[0])
		return false;
	g_banner[0] = '\0';
	g_bannerBlank = false;
	return true;
}

// Show text on the screen now
static void dosBannerShow(const char *text) {
	int count = 0;
	SDL_Window **windows = SDL_GetWindows(&count);
	SDL_Window *window = count ? windows[0] : nullptr;
	SDL_free(windows);
	SDL_Surface *surface = window ? SDL_GetWindowSurface(window) : nullptr;
	if (!surface || !*text)
		return;

	Common::strlcpy(g_banner, text, sizeof(g_banner));
	// Before a game starts, the banner goes on a blank screen
	g_bannerBlank = g_engine == nullptr;
	if (dosBannerDraw(surface))
		SDL_UpdateWindowSurface(window);
}

// The MIDI driver calls this while it waits on the MPU-401. SDL's DOS threads
// are cooperative and the sound card is fed from one of them, so give it a turn.
void dosPumpAudio() {
	SDL_Delay(0);
}

OSystem_SDL_DOS::OSystem_SDL_DOS() :
	_shotEveryMs(0), _shotsTaken(0), _shotAtMs(0), _quitAtMs(0), _quitSent(false), _earlyLogDone(false) {
	_lastError[0] = '\0';
	_fsFactory = new POSIXFilesystemFactory();
}

// SDL's own messages go through logMessage(): stderr would be drawn over the graphics
static void SDLCALL dosSdlLog(void *userdata, int category, SDL_LogPriority priority, const char *message) {
	const LogMessageType::Type type = priority >= SDL_LOG_PRIORITY_ERROR ? LogMessageType::kError :
	                                  priority >= SDL_LOG_PRIORITY_WARN ? LogMessageType::kWarning : LogMessageType::kDebug;
	g_system->logMessage(type, (Common::String("SDL: ") + message + "\n").c_str());
}

void OSystem_SDL_DOS::initBackend() {
	SDL_SetLogOutputFunction(dosSdlLog, nullptr);

	// Savegames whose names do not fit 8.3 are found with or without a long file name driver
	_savefileManager = dosCreateSaveFileManager();

	#ifdef USE_SDL_NET
	// Warns if there is no TCP/IP stack for the local web server
	dosNetworkAvailable();
	#endif

	// 2048-frame device buffers (SDL's default is 1024), so a game loop that polls only every 100 ms or so
	// plays without breaks, at the price of more latency (SDL_AUDIO_DEVICE_SAMPLE_FRAMES overrides it)
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "2048");

	// SDL's own event source, behind one that gives the sound thread its turns
	_eventSource = new SdlEventSource();
	_eventSourceWrapper = new DOSAudioPumpEventSource(_eventSource);

	OSystem_SDL::initBackend();
	flushEarlyLog();

	// SCUMMVM_DOS_SHOTS=n: a screenshot every n seconds, as in the own backend
	if (const char *shots = getenv("SCUMMVM_DOS_SHOTS")) {
		_shotEveryMs = (uint32)atoi(shots) * 1000;
		_shotAtMs = _shotEveryMs;
	}
	if (const char *quit = getenv("SCUMMVM_DOS_QUITAT"))
		_quitAtMs = (uint32)atoi(quit) * 1000;
}

void OSystem_SDL_DOS::delayMillis(uint msecs) {
	const uint32 now = getMillis();

	if (_shotEveryMs && now >= _shotAtMs) {
		Common::String name = Common::String::format("SHOT%04u.PNG", (uint)++_shotsTaken);
		_shotAtMs = now + _shotEveryMs;
		if (SdlGraphicsManager *gfx = dynamic_cast<SdlGraphicsManager *>(_graphicsManager))
			warning("DOS test: screenshot %s %s", name.c_str(), gfx->saveScreenshot(Common::Path(name)) ? "saved" : "FAILED");
	}

	if (_quitAtMs && !_quitSent && now >= _quitAtMs) {
		_quitSent = true;
		Common::Event quitEvent;
		quitEvent.type = Common::EVENT_QUIT;
		_eventManager->pushEvent(quitEvent);
		warning("DOS test: quitting");
	}

	OSystem_SDL::delayMillis(msecs);
}

// Write what was logged before the log file was open (OSystem_SDL::initBackend() opens it)
void OSystem_SDL_DOS::flushEarlyLog() {
	if (_logger && !_logFilePath.empty() && !_earlyLog.empty())
		_logger->print(_earlyLog.c_str());
	_earlyLog.clear();
	_earlyLogDone = true;
}

void OSystem_SDL_DOS::logMessage(LogMessageType::Type type, const char *message) {
	if (!_earlyLogDone) {
		if (_logger && !_logFilePath.empty())
			flushEarlyLog();
		else if (_earlyLog.size() < 65536)
			_earlyLog += message;
	}

	// With a window open the screen is in a graphics mode: messages go to the log file only
	int windows = 0;
	SDL_free(SDL_GetWindows(&windows));
	if (!windows) {
		OSystem_SDL::logMessage(type, message);
		return;
	}

	if (type == LogMessageType::kError) {
		strncpy(_lastError, message, sizeof(_lastError) - 1);
		_lastError[sizeof(_lastError) - 1] = '\0';
	}

	if (_logger)
		_logger->print(message);

	// Info messages (such as the game starting) as a banner, but not over the launcher
	if (type == LogMessageType::kInfo && !(GUI::GuiManager::hasInstance() && g_gui.isActive()))
		dosBannerShow(message);
}

void OSystem_SDL_DOS::engineInit() {
	OSystem_SDL::engineInit();
	dosOpenMPTEngineInit(ConfMan.get("engineid").c_str());
}

void OSystem_SDL_DOS::quit() {
	// Clean up and leave through _exit(): exit() would run atexit handlers of unloaded plug-ins
	dosOplSilence();
	#ifdef USE_SDL_NET
	dosNetworkStop();
	#endif
	// (destroy() deletes this object)
	char lastError[sizeof(_lastError)];
	memcpy(lastError, _lastError, sizeof(lastError));
	destroy();
	if (lastError[0]) {
		fputs(lastError, stderr);
		fflush(stderr);
	}
	fflush(nullptr);
	_exit(0);
}

void OSystem_SDL_DOS::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	// The working directory and DATA inside it, by 8.3 names too
	dosAddDataFolders(s, priority - 1);
}

Common::String OSystem_SDL_DOS::getSystemLanguage() const {
	// From the country set in CONFIG.SYS
	return dosSystemLanguage();
}

Common::Path OSystem_SDL_DOS::getDefaultLogFileName() {
	return Common::Path("scummvm.log");
}

AudioCDManager *OSystem_SDL_DOS::createAudioCDManager() {
	// Audio CDs through MSCDEX, and the tracks as files when the game has them
	return dosCreateAudioCDManager();
}

#endif
