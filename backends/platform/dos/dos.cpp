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

// 32-bit MS-DOS backend (DJGPP, protected mode through a DPMI host)
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <dpmi.h>
#include <go32.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "common/scummsys.h"

#if defined(USE_DOS_DRIVER)

#include "common/config-manager.h"
#include "common/events.h"
#include "common/fs.h"
#include "common/textconsole.h"

#include "backends/events/default/default-events.h"
#include "backends/log/log.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "backends/modular-backend.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "base/main.h"
#include "gui/gui-manager.h"

#ifdef DYNAMIC_MODULES
	#include "backends/plugins/dos/dos-provider.h"
#endif
#include "backends/platform/dos/dos-cdaudio.h"
#include "backends/platform/dos/dos-files.h"
#include "backends/platform/dos/dos-graphics.h"
#include "backends/platform/dos/dos-input.h"
#include "backends/platform/dos/dos-keyboard.h"
#include "backends/platform/dos/dos-mixer.h"
#ifdef USE_SDL_NET
	#include "backends/platform/dos/dos-net.h"
#endif

class OSystem_DOS : public ModularMixerBackend, public ModularGraphicsBackend, Common::EventSource {
public:
	OSystem_DOS();
	~OSystem_DOS() override;

	void initBackend() override;

	bool pollEvent(Common::Event &event) override;

	Common::MutexInternal *createMutex() override;
	uint32 getMillis(bool skipRecord = false) override;
	void delayMillis(uint msecs) override;
	void updateAudio(); ///< also called by the MIDI driver while it waits (dosPumpAudio())
	void getTimeAndDate(TimeDate &td, bool skipRecord = false) const override;

	void quit() override;

	void logMessage(LogMessageType::Type type, const char *message) override;

	void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override;
	Common::String getSystemLanguage() const override;

private:
	Common::WriteStream *createLogFile();
	void openLog();
	enum { kQueueSize = 32 };

	void pushEvent(const Common::Event &event);
	void pollInput();

	DOSGraphicsManager *_dosGfx;
	DOSMixerManager *_dosMixer;

	bool _selfTestsDone;

	uclock_t _startClock;
	Backends::Log::Log *_logger;

	// Messages logged before the log file was opened
	Common::String *_earlyLog;

	// Printed again on exit, as errors raised in a graphics mode only go to the log
	char _lastError[512];

	// Input state
	Common::Event _queue[kQueueSize];
	int _queueHead, _queueCount;
	int _mouseX, _mouseY, _mouseButtons;

	// Testing aid, from SCUMMVM_DOS_CLICKS="seconds:x,y;...": a left click at x,y at each time
	struct ClickStep {
		uint32 ms;
		int x, y;
	};
	enum { kMaxClicks = 16 };
	ClickStep _clicks[kMaxClicks];
	int _clickCount, _clickNext, _clickPhase;
	uint32 _clickAt;

	void parseScriptedClicks(const char *spec);
	void scriptedClicks();

	// Testing aid, from SCUMMVM_DOS_RTL="seconds": return to the launcher at that time
	uint32 _rtlAtMs;
	bool _rtlSent;

	// Testing aid, from SCUMMVM_DOS_QUITAT="seconds": quit at that time
	uint32 _quitAtMs;
	bool _quitSent;
};

OSystem_DOS::OSystem_DOS() :
	_dosGfx(nullptr),
	_dosMixer(nullptr),
	_selfTestsDone(false),
	_startClock(0),
	_logger(nullptr),
	_earlyLog(nullptr),
	_queueHead(0), _queueCount(0),
	_mouseX(-1), _mouseY(-1), _mouseButtons(0),
	_clickCount(0), _clickNext(0), _clickPhase(0), _clickAt(0),
	_rtlAtMs(0), _rtlSent(false), _quitAtMs(0), _quitSent(false) {
	_lastError[0] = '\0';
	_fsFactory = new POSIXFilesystemFactory();
}

// The one backend, for the MIDI driver (see dosPumpAudio())
static OSystem_DOS *g_dosSystem = nullptr;

OSystem_DOS::~OSystem_DOS() {
	g_dosSystem = nullptr;
	// A run that never got as far as initBackend() (--add, --list-games) still has its log
	openLog();
	delete _logger;
	_logger = nullptr;
}

// As OSystem_SDL::createLogFile(): the "logfile" setting, or scummvm.log
Common::WriteStream *OSystem_DOS::createLogFile() {
	Common::Path logFile("scummvm.log");
	if (ConfMan.hasKey("logfile"))
		logFile = ConfMan.getPath("logfile");
	if (logFile.empty())
		return nullptr;

	Common::FSNode file(logFile);
	return file.createWriteStream(false);
}

// Open the log file (once) and write the early messages to it
void OSystem_DOS::openLog() {
	if (_logger)
		return;

	_logger = new Backends::Log::Log(this);
	if (Common::WriteStream *logFile = createLogFile()) {
		_logger->open(logFile);
		if (_earlyLog)
			_logger->print(_earlyLog->c_str());
	}
	delete _earlyLog;
	_earlyLog = nullptr;
}

void OSystem_DOS::initBackend() {
	g_dosSystem = this;
	_startClock = uclock();

	openLog();

	// The 320x200 menu's bitmap font is distorted when scaled
	ConfMan.registerDefault("gui_disable_fixed_font_scaling", true);

	_dosGfx = new DOSGraphicsManager();

	_timerManager = new DefaultTimerManager();
	_eventManager = new DefaultEventManager(this);
	_savefileManager = dosCreateSaveFileManager();
	_graphicsManager = _dosGfx;
	_dosMixer = new DOSMixerManager(); // Sound Blaster, or silence without one
	_mixerManager = _dosMixer;
	_mixerManager->init();

	dosKeyboardInit();
	#ifdef USE_SDL_NET
	// Warns if there is no TCP/IP stack for the local web server
	dosNetworkAvailable();
	#endif
	if (getenv("SCUMMVM_DOS_JOYSTICK")) {
		if (dosJoystickInit())
			warning("DOS: joystick found on the game port (moves the cursor, buttons click)");
		else
			warning("DOS: SCUMMVM_DOS_JOYSTICK is set but no joystick answered on the game port");
	}
	if (const char *clicks = getenv("SCUMMVM_DOS_CLICKS"))
		parseScriptedClicks(clicks);

	if (const char *rtl = getenv("SCUMMVM_DOS_RTL"))
		_rtlAtMs = (uint32)atoi(rtl) * 1000;
	if (const char *quit = getenv("SCUMMVM_DOS_QUITAT"))
		_quitAtMs = (uint32)atoi(quit) * 1000;

	#ifdef USE_RGB_COLOR
	// Testing aid: exercise the high colour screen without a game, then stop
	if (getenv("SCUMMVM_DOS_SELFTEST16")) {
		_dosGfx->selfTest16();
		quit();
	}
	#endif

	// Audio CDs through MSCDEX, and the tracks as files when the game has them
	_audiocdManager = dosCreateAudioCDManager();

	BaseBackend::initBackend();
}

void OSystem_DOS::updateAudio() {
	_dosMixer->update();
}

// For the MIDI driver, which can wait for a while on the MPU-401
void dosPumpAudio() {
	if (g_dosSystem)
		g_dosSystem->updateAudio();
}

void OSystem_DOS::parseScriptedClicks(const char *spec) {
	while (*spec && _clickCount < kMaxClicks) {
		char *end;
		long sec = strtol(spec, &end, 10);
		if (*end != ':')
			break;
		spec = end + 1;
		long x = strtol(spec, &end, 10);
		if (*end != ',')
			break;
		spec = end + 1;
		long y = strtol(spec, &end, 10);

		_clicks[_clickCount].ms = (uint32)sec * 1000;
		_clicks[_clickCount].x = (int)x;
		_clicks[_clickCount].y = (int)y;
		_clickCount++;

		spec = end;
		if (*spec == ';')
			spec++;
	}
	warning("DOS scripted clicks: %d", _clickCount);
}

// Move the mouse driver's cursor, then press and release the left button
void OSystem_DOS::scriptedClicks() {
	if (_clickNext >= _clickCount)
		return;

	uint32 now = getMillis();
	const ClickStep &c = _clicks[_clickNext];

	if (_clickPhase == 0 && now >= c.ms) {
		dosMouseSetPosition(c.x, c.y);
		_clickPhase = 1;
		_clickAt = now + 300;
	} else if (_clickPhase == 1 && now >= _clickAt) {
		Common::Event ev;
		int gx, gy;
		_dosGfx->getGameMouse(gx, gy);
		ev.type = Common::EVENT_LBUTTONDOWN;
		ev.mouse.x = gx;
		ev.mouse.y = gy;
		pushEvent(ev);
		warning("DOS scripted click %d at %d,%d (game %d,%d)", _clickNext + 1, c.x, c.y, gx, gy);
		_clickPhase = 2;
		_clickAt = now + 150;
	} else if (_clickPhase == 2 && now >= _clickAt) {
		Common::Event ev;
		int gx, gy;
		_dosGfx->getGameMouse(gx, gy);
		ev.type = Common::EVENT_LBUTTONUP;
		ev.mouse.x = gx;
		ev.mouse.y = gy;
		pushEvent(ev);
		_clickPhase = 0;
		_clickNext++;
	}
}

void OSystem_DOS::pushEvent(const Common::Event &event) {
	if (_queueCount >= kQueueSize)
		return;
	_queue[(_queueHead + _queueCount) % kQueueSize] = event;
	_queueCount++;
}

void OSystem_DOS::pollInput() {
	// Mouse: report moves and button changes
	int x = _mouseX, y = _mouseY, buttons = 0;
	int pressed[3], released[3];

	// The joystick moves the mouse driver's cursor
	int joyButtons = 0;
	dosJoystickUpdate(getMillis(), joyButtons);

	dosMousePoll(x, y, buttons, pressed, released);
	buttons |= joyButtons;

	if (x != _mouseX || y != _mouseY) {
		int dx = (_mouseX < 0) ? 0 : x - _mouseX;
		int dy = (_mouseY < 0) ? 0 : y - _mouseY;
		_mouseX = x;
		_mouseY = y;
		_dosGfx->setHostMouse(x, y);

		Common::Event ev;
		ev.type = Common::EVENT_MOUSEMOVE;
		int gx, gy;
		_dosGfx->getGameMouse(gx, gy);
		ev.mouse.x = gx;
		ev.mouse.y = gy;
		ev.relMouse.x = dx;
		ev.relMouse.y = dy;
		pushEvent(ev);
	}

	static const struct {
		int mask;
		Common::EventType down, up;
	} buttonMap[] = {
		{ 1, Common::EVENT_LBUTTONDOWN, Common::EVENT_LBUTTONUP },
		{ 2, Common::EVENT_RBUTTONDOWN, Common::EVENT_RBUTTONUP },
		{ 4, Common::EVENT_MBUTTONDOWN, Common::EVENT_MBUTTONUP }
	};

	for (int i = 0; i < 3; i++) {
		bool now = (buttons & buttonMap[i].mask) != 0;
		bool before = (_mouseButtons & buttonMap[i].mask) != 0;

		Common::Event ev;
		int gx, gy;
		_dosGfx->getGameMouse(gx, gy);
		ev.mouse.x = gx;
		ev.mouse.y = gy;

		if (now != before) {
			ev.type = now ? buttonMap[i].down : buttonMap[i].up;
			pushEvent(ev);
		} else if (!now && pressed[i] > 0) {
			// A whole click (down and up) happened between two polls
			ev.type = buttonMap[i].down;
			pushEvent(ev);
			ev.type = buttonMap[i].up;
			pushEvent(ev);
		}
	}
	_mouseButtons = buttons;

	// The wheel, if the mouse driver has one (a few clicks at most per poll)
	int wheel = dosMouseTakeWheel();
	for (int n = 0; wheel != 0 && n < 8; n++) {
		Common::Event ev;
		ev.type = (wheel > 0) ? Common::EVENT_WHEELUP : Common::EVENT_WHEELDOWN;
		int gx, gy;
		_dosGfx->getGameMouse(gx, gy);
		ev.mouse.x = gx;
		ev.mouse.y = gy;
		pushEvent(ev);
		wheel += (wheel > 0) ? -1 : 1;
	}

	scriptedClicks();

	// Keyboard; keys that don't fit in the queue wait in the keyboard buffer
	DosKey key;
	while (_queueCount < kQueueSize - 2 && dosKeyboardNextKey(key)) {
		// Alt+S takes a screenshot (SHOT0001.BMP, ...), as in the other ports
		if (key.pressed && !key.repeat && key.state.keycode == Common::KEYCODE_s && (key.state.flags & Common::KBD_ALT)) {
			_dosGfx->saveScreenshot();
			continue;
		}

		Common::Event ev;
		ev.type = key.pressed ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
		ev.kbd = key.state;
		ev.kbdRepeat = key.repeat;
		pushEvent(ev);
	}
}

void dosOpl3SelfTest();

bool OSystem_DOS::pollEvent(Common::Event &event) {
	if (!_selfTestsDone) {
		_selfTestsDone = true;
		if (getenv("SCUMMVM_DOS_OPL3TEST"))
			dosOpl3SelfTest(); // ScummVM's configuration is loaded by now
		if (getenv("SCUMMVM_DOS_KBDTEST"))
			dosKeyboardSelfTest();
		if (const char *cdMode = getenv("SCUMMVM_DOS_CDTEST"))
			dosCdSelfTest(cdMode);
		#ifdef DYNAMIC_MODULES
		if (const char *engines = getenv("SCUMMVM_DOS_PLUGINTEST"))
			dosPluginSelfTest(engines);
		#endif

		// Testing aid: quit at the first event, after any self-tests
		if (getenv("SCUMMVM_DOS_TESTEXIT")) {
			Common::Event quit;
			quit.type = Common::EVENT_QUIT;
			pushEvent(quit);
			// Free memory, for tests/check-plugin-ram.sh
			warning("DOS test: the program is running (its first event): leaving; free memory %lu KB, free virtual memory %lu KB",
			        (unsigned long)(_go32_dpmi_remaining_physical_memory() / 1024),
			        (unsigned long)(_go32_dpmi_remaining_virtual_memory() / 1024));
		}
	}

	if (_rtlAtMs && !_rtlSent && getMillis() >= _rtlAtMs) {
		_rtlSent = true;
		Common::Event ev;
		ev.type = Common::EVENT_RETURN_TO_LAUNCHER;
		pushEvent(ev);
		warning("DOS test: returning to the launcher");
	}
	if (_quitAtMs && !_quitSent && getMillis() >= _quitAtMs) {
		_quitSent = true;
		Common::Event ev;
		ev.type = Common::EVENT_QUIT;
		pushEvent(ev);
		warning("DOS test: quitting; free memory %lu KB, free virtual memory %lu KB",
		        (unsigned long)(_go32_dpmi_remaining_physical_memory() / 1024),
		        (unsigned long)(_go32_dpmi_remaining_virtual_memory() / 1024));
	}
	((DefaultTimerManager *)getTimerManager())->checkTimers();
	updateAudio();

	if (_queueCount == 0)
		pollInput();

	if (_queueCount == 0)
		return false;

	event = _queue[_queueHead];
	_queueHead = (_queueHead + 1) % kQueueSize;
	_queueCount--;
	return true;
}

Common::MutexInternal *OSystem_DOS::createMutex() {
	return new NullMutexInternal();
}

uint32 OSystem_DOS::getMillis(bool skipRecord) {
	// uclock() counts ticks of the 1.19318 MHz timer chip
	uclock_t elapsed = uclock() - _startClock;
	return (uint32)((elapsed * 1000) / UCLOCKS_PER_SEC);
}

void OSystem_DOS::delayMillis(uint msecs) {
	if (!msecs)
		return;

	uint32 end = getMillis() + msecs;
	uint32 now;
	do {
		// Nothing else feeds the sound card while the engine waits
		updateAudio();

		// Timers drive the FM music, so keep them running while we wait
		if (_timerManager)
			((DefaultTimerManager *)_timerManager)->checkTimers();

		now = getMillis();

		// Yield only for long waits: __dpmi_yield() can sleep for a whole 55 ms timer tick
		if (now < end && end - now > 80)
			__dpmi_yield();
	} while (now < end);
}

void OSystem_DOS::getTimeAndDate(TimeDate &td, bool skipRecord) const {
	time_t curTime = time(0);
	struct tm t = *localtime(&curTime);
	td.tm_sec = t.tm_sec;
	td.tm_min = t.tm_min;
	td.tm_hour = t.tm_hour;
	td.tm_mday = t.tm_mday;
	td.tm_mon = t.tm_mon;
	td.tm_year = t.tm_year;
	td.tm_wday = t.tm_wday;
}

void dosSoundBlasterShutdown();
void dosOplSilence();

void OSystem_DOS::quit() {
	// Clean up and leave through _exit(): exit() would run atexit handlers of unloaded plug-ins
	if (_dosGfx)
		_dosGfx->restoreTextMode();
	dosSoundBlasterShutdown();
	dosOplSilence();
	dosKeyboardRemove();
	#ifdef USE_SDL_NET
	dosNetworkStop();
	#endif
	openLog();
	delete _logger;
	_logger = nullptr;
	if (_lastError[0]) {
		fputs(_lastError, stderr);
		fflush(stderr);
	}
	fflush(nullptr);
	_exit(0);
}

void OSystem_DOS::logMessage(LogMessageType::Type type, const char *message) {
	// Text printed in a graphics mode shows as garbage
	const bool graphics = _dosGfx && _dosGfx->isGraphicsMode();

	if (type == LogMessageType::kError) {
		strncpy(_lastError, message, sizeof(_lastError) - 1);
		_lastError[sizeof(_lastError) - 1] = '\0';
	}

	// Every message goes to the log file (once it is open)
	if (_logger) {
		_logger->print(message);
	} else {
		if (!_earlyLog)
			_earlyLog = new Common::String();
		if (_earlyLog->size() < 65536)
			*_earlyLog += message;
	}

	if (graphics) {
		// Info messages (such as the game starting) as a banner, but not over the launcher
		if (type == LogMessageType::kInfo && _dosGfx && !(GUI::GuiManager::hasInstance() && g_gui.isActive()))
			_dosGfx->showBanner(message);
		return;
	}

	FILE *output = (type == LogMessageType::kInfo || type == LogMessageType::kDebug) ? stdout : stderr;
	fputs(message, output);
	fflush(output);
}

void OSystem_DOS::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	// The working directory and DATA inside it, by 8.3 names too
	dosAddDataFolders(s, priority - 1);
}

Common::String OSystem_DOS::getSystemLanguage() const {
	// From the country set in CONFIG.SYS
	return dosSystemLanguage();
}

int main(int argc, char *argv[]) {
	g_system = new OSystem_DOS();
	assert(g_system);

	#ifdef DYNAMIC_MODULES
	// Engines are DXE3 plug-ins in the PLUGINS folder
	PluginManager::instance().addPluginProvider(new DOSPluginProvider());
	#endif

	// Invoke the actual ScummVM main entry point:
	int res = scummvm_main(argc, argv);
	g_system->destroy();
	return res;
}

#endif /* USE_DOS_DRIVER */
