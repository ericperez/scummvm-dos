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

// The program's side of OPENMPT.DXE: the libopenmpt functions ScummVM calls, passed on to the module

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/scummsys.h"

#include "backends/platform/dos/dos-openmpt.h"

#if defined(DOS_OPENMPT_DXE) && defined(USE_DOS_DRIVER)

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio/audiostream.h"
#include "audio/mods/universaltracker.h"
#include "backends/plugins/dos/dos-provider.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/ptr.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"
#include "common/util.h"

namespace {

void *g_handle = nullptr;
const DosOpenMPTApi *g_api = nullptr;
/** Modules made by OPENMPT.DXE and not destroyed yet: it stays loaded while there are any */
int g_openModules = 0;
/** A failed load is not tried again until the next game starts (one warning, not one per tune) */
bool g_loadFailed = false;
unsigned g_heapBeforeLoad = 0;

bool logging() {
	return getenv("SCUMMVM_DOS_PLUGLOG") != nullptr;
}

void unload() {
	const unsigned heapLoaded = dosHeapInUse();
	dlclose(g_handle);
	g_handle = nullptr;
	g_api = nullptr;
	if (logging())
		warning("DOSOPENMPT: unloaded %s, %d KB of memory given back", kDosOpenMPTFile, (int)((int)heapLoaded - (int)dosHeapInUse()) / 1024);
}

bool load() {
	if (g_api)
		return true;
	if (g_loadFailed)
		return false;

	const uclock_t begin = uclock();
	g_heapBeforeLoad = dosHeapInUse();
	g_handle = dlopen(kDosOpenMPTFile, RTLD_LAZY);
	if (!g_handle) {
		warning("Tracker music is off: %s could not be loaded (%s)", kDosOpenMPTFile, dlerror());
		g_loadFailed = true;
		return false;
	}

	// The name without DJGPP's leading underscore first, then with it (as dos-provider.cpp)
	void *symbol = dlsym(g_handle, "dosOpenMPTGetApi");
	if (!symbol)
		symbol = dlsym(g_handle, "_dosOpenMPTGetApi");
	const DosOpenMPTApi *(*getApi)() = nullptr;
	if (symbol) {
		assert(sizeof(getApi) == sizeof(symbol));
		memcpy(&getApi, &symbol, sizeof(getApi));
	}
	const DosOpenMPTApi *api = getApi ? getApi() : nullptr;
	if (!api || api->version != kDosOpenMPTApiVersion) {
		warning("Tracker music is off: %s is not the module this program needs", kDosOpenMPTFile);
		dlclose(g_handle);
		g_handle = nullptr;
		g_loadFailed = true;
		return false;
	}
	g_api = api;

	if (logging())
		warning("DOSOPENMPT: loaded %s in %u ms, %u KB of memory", kDosOpenMPTFile,
		        (unsigned)((uclock() - begin) * 1000 / UCLOCKS_PER_SEC), (dosHeapInUse() - g_heapBeforeLoad) / 1024);
	return true;
}

// SCUMMVM_DOS_MPTTEST=<file>[;<file>...]: play a second of each module file through OPENMPT.DXE as a game starts
void selfTest(const char *fileName) {
	Common::File *file = new Common::File();
	if (!file->open(Common::FSNode(Common::Path(fileName, Common::Path::kNativeSeparator)))) {
		warning("DOSOPENMPT: self-test FAILED: cannot open %s", fileName);
		delete file;
		return;
	}

	Common::ScopedPtr<Audio::RewindableAudioStream> stream(Audio::makeUniversalTrackerStream(file, DisposeAfterUse::YES));
	if (!stream) {
		warning("DOSOPENMPT: self-test FAILED: %s did not open as a module", fileName);
		return;
	}

	int16 buffer[2 * 1024];
	int frames = 0;
	int peak = 0;
	while (frames < stream->getRate() && !stream->endOfData()) {
		const int samples = stream->readBuffer(buffer, ARRAYSIZE(buffer));
		for (int i = 0; i < samples; i++)
			peak = MAX<int>(peak, ABS<int>(buffer[i]));
		frames += samples / 2;
		if (samples == 0)
			break;
	}
	warning("DOSOPENMPT: self-test: %s played, %d frames at %d Hz, peak %d, %d module(s) open", fileName, frames,
	        stream->getRate(), peak, g_openModules);
}

} // End of anonymous namespace

void dosOpenMPTEngineInit(const char *engineId) {
	g_loadFailed = false;
	if (!dosEngineUsesTracker(engineId))
		return;
	if (load()) {
		if (const char *testFiles = getenv("SCUMMVM_DOS_MPTTEST")) {
			Common::StringTokenizer files(testFiles, ";");
			while (!files.empty())
				selfTest(files.nextToken().c_str());
		}
	}
}

void dosOpenMPTRelease() {
	if (g_handle && g_openModules == 0)
		unload();
}

// Another engine's game, or a module opened before any game ran, loads OPENMPT.DXE at its first tune
openmpt_module *openmpt_module_create2(openmpt_stream_callbacks streamCallbacks, void *stream, openmpt_log_func logFunc,
                                       void *logUser, openmpt_error_func errorFunc, void *errorUser, int *error,
                                       const char **errorMessage, const openmpt_module_initial_ctl *ctls) {
	if (!load()) {
		if (error)
			*error = OPENMPT_ERROR_GENERAL;
		if (errorMessage)
			*errorMessage = nullptr;
		return nullptr;
	}
	openmpt_module *mod = g_api->moduleCreate2(streamCallbacks, stream, logFunc, logUser, errorFunc, errorUser, error,
	                      errorMessage, ctls);
	if (mod)
		g_openModules++;
	return mod;
}

void openmpt_module_destroy(openmpt_module *mod) {
	if (!mod)
		return;
	assert(g_api);
	g_api->moduleDestroy(mod);
	g_openModules--;
}

size_t openmpt_module_read_interleaved_stereo(openmpt_module *mod, int32_t sampleRate, size_t count, int16_t *interleavedStereo) {
	assert(g_api);
	return g_api->readInterleavedStereo(mod, sampleRate, count, interleavedStereo);
}

double openmpt_module_set_position_seconds(openmpt_module *mod, double seconds) {
	assert(g_api);
	return g_api->setPositionSeconds(mod, seconds);
}

const char *openmpt_error_string(int error) {
	if (!g_api)
		return strdup("OPENMPT.DXE is not loaded");
	return g_api->errorString(error);
}

void openmpt_free_string(const char *str) {
	// Both kinds of string come from the program's malloc (the module's free() is the program's)
	if (!g_api)
		free(const_cast<char *>(str));
	else
		g_api->freeString(str);
}

#endif // DOS_OPENMPT_DXE && USE_DOS_DRIVER
