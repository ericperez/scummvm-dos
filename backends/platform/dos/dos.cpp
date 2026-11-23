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
#include "backends/graphics/null/null-graphics.h"
#include "backends/log/log.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "backends/mixer/null/null-mixer.h"
#include "backends/modular-backend.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "base/main.h"

class OSystem_DOS : public ModularMixerBackend, public ModularGraphicsBackend, Common::EventSource {
public:
	OSystem_DOS();
	~OSystem_DOS() override;

	void initBackend() override;

	bool pollEvent(Common::Event &event) override;

	Common::MutexInternal *createMutex() override;
	uint32 getMillis(bool skipRecord = false) override;
	void delayMillis(uint msecs) override;
	void getTimeAndDate(TimeDate &td, bool skipRecord = false) const override;

	void quit() override;

	void logMessage(LogMessageType::Type type, const char *message) override;

private:
	Common::WriteStream *createLogFile();
	void openLog();
	enum { kQueueSize = 32 };

	void pushEvent(const Common::Event &event);

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

	// Testing aid, from SCUMMVM_DOS_RTL="seconds": return to the launcher at that time
	uint32 _rtlAtMs;
	bool _rtlSent;

	// Testing aid, from SCUMMVM_DOS_QUITAT="seconds": quit at that time
	uint32 _quitAtMs;
	bool _quitSent;
};

OSystem_DOS::OSystem_DOS() :
	_selfTestsDone(false),
	_startClock(0),
	_logger(nullptr),
	_earlyLog(nullptr),
	_queueHead(0), _queueCount(0),
	_rtlAtMs(0), _rtlSent(false), _quitAtMs(0), _quitSent(false) {
	_lastError[0] = '\0';
	_fsFactory = new POSIXFilesystemFactory();
}

OSystem_DOS::~OSystem_DOS() {
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
	_startClock = uclock();

	openLog();

	_timerManager = new DefaultTimerManager();
	_eventManager = new DefaultEventManager(this);
	_savefileManager = new DefaultSaveFileManager();
	_graphicsManager = new NullGraphicsManager();
	_mixerManager = new NullMixerManager();
	_mixerManager->init();

	if (const char *rtl = getenv("SCUMMVM_DOS_RTL"))
		_rtlAtMs = (uint32)atoi(rtl) * 1000;
	if (const char *quit = getenv("SCUMMVM_DOS_QUITAT"))
		_quitAtMs = (uint32)atoi(quit) * 1000;

	BaseBackend::initBackend();
}

void OSystem_DOS::pushEvent(const Common::Event &event) {
	if (_queueCount >= kQueueSize)
		return;
	_queue[(_queueHead + _queueCount) % kQueueSize] = event;
	_queueCount++;
}

bool OSystem_DOS::pollEvent(Common::Event &event) {
	if (!_selfTestsDone) {
		_selfTestsDone = true;

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

void OSystem_DOS::quit() {
	// Clean up and leave through _exit(): exit() would run atexit handlers of unloaded plug-ins
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

	FILE *output = (type == LogMessageType::kInfo || type == LogMessageType::kDebug) ? stdout : stderr;
	fputs(message, output);
	fflush(output);
}

int main(int argc, char *argv[]) {
	g_system = new OSystem_DOS();
	assert(g_system);

	// Invoke the actual ScummVM main entry point:
	int res = scummvm_main(argc, argv);
	g_system->destroy();
	return res;
}

#endif /* USE_DOS_DRIVER */
