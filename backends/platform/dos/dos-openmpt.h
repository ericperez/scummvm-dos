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

#ifndef BACKENDS_PLATFORM_DOS_OPENMPT_H
#define BACKENDS_PLATFORM_DOS_OPENMPT_H

// In a plug-in build libopenmpt is a DXE module of its own, OPENMPT.DXE next to the program, loaded
// only while a game of an engine with tracker music runs (see PLUGINS.md)

#include <string.h>

/** Next to the program, in the folder it runs in; not an engine plug-in */
static const char *const kDosOpenMPTFile = "OPENMPT.DXE";

#if defined(DYNAMIC_MODULES) && defined(USE_OPENMPT)
#define DOS_OPENMPT_DXE

#include <libopenmpt/libopenmpt.h>

/** The libopenmpt functions ScummVM calls (audio/mods/universaltracker.cpp), as OPENMPT.DXE gives them */
struct DosOpenMPTApi {
	int version;
	openmpt_module *(*moduleCreate2)(openmpt_stream_callbacks streamCallbacks, void *stream, openmpt_log_func logFunc,
	                                 void *logUser, openmpt_error_func errorFunc, void *errorUser, int *error,
	                                 const char **errorMessage, const openmpt_module_initial_ctl *ctls);
	void (*moduleDestroy)(openmpt_module *mod);
	size_t (*readInterleavedStereo)(openmpt_module *mod, int32_t sampleRate, size_t count, int16_t *interleavedStereo);
	double (*setPositionSeconds)(openmpt_module *mod, double seconds);
	const char *(*errorString)(int error);
	void (*freeString)(const char *str);
};

enum {
	kDosOpenMPTApiVersion = 1   ///< DosOpenMPTApi::version of this layout of the structure
};

/** The one symbol OPENMPT.DXE exports */
extern "C" const DosOpenMPTApi *dosOpenMPTGetApi();

/** A game is about to run: load OPENMPT.DXE if its engine plays tracker music */
void dosOpenMPTEngineInit(const char *engineId);

/** A plug-in was unloaded: unload OPENMPT.DXE too, once no module is left open */
void dosOpenMPTRelease();

#else

/** Tracker music is in the program, or not built: nothing to load */
inline void dosOpenMPTEngineInit(const char *) {}
/** Tracker music is in the program, or not built: nothing to unload */
inline void dosOpenMPTRelease() {}

#endif

/** The engines whose configure.engine needs universaltracker (tests/check-openmpt-dxe.sh keeps the two the same) */
inline bool dosEngineUsesTracker(const char *engineId) {
	static const char *const engines[] = { "ags", "sludge", "testbed" };
	for (const char *engine : engines) {
		if (strcmp(engine, engineId) == 0)
			return true;
	}
	return false;
}

#endif
