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

#ifndef BACKENDS_PLATFORM_DOS_SDL_H
#define BACKENDS_PLATFORM_DOS_SDL_H

#include "backends/platform/sdl/sdl.h"

/**
 * The DOS port on SDL3's DOS driver (--backend=sdl): SDL does video, input and PCM output;
 * the FM chip, MPU-401, CD audio, 8.3 names, TCP/IP and plug-ins come from the dos backend.
 */
class OSystem_SDL_DOS : public OSystem_SDL {
public:
	OSystem_SDL_DOS();

	void initBackend() override;

	void delayMillis(uint msecs) override;

	void logMessage(LogMessageType::Type type, const char *message) override;

	void quit() override;
	void fatalError() override;

	void engineInit() override;

	void addSysArchivesToSearchSet(Common::SearchSet &s, int priority = 0) override;
	Common::String getSystemLanguage() const override;

protected:
	Common::Path getDefaultLogFileName() override;

	AudioCDManager *createAudioCDManager() override;

private:
	// Testing aids, as in dos.cpp: SCUMMVM_DOS_SHOTS=s (screenshots) and SCUMMVM_DOS_QUITAT=s

	uint32 _shotEveryMs, _shotsTaken, _shotAtMs;
	uint32 _quitAtMs;
	bool _quitSent;

	/** The last error, printed when the program leaves, once the text mode is back */
	char _lastError[512];

	/** Messages logged before the log file was open (such as SDL_net's start), written to it once it is */
	Common::String _earlyLog;
	bool _earlyLogDone;
	void flushEarlyLog();

	/** quit() and fatalError(): shut down and leave with this exit status */
	void leave(int status);
};

#endif
