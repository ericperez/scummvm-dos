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

#include <sys/nearptr.h>

#include "backends/platform/dos/dos-mouse.h"
#include "backends/platform/dos/dos-sdl.h"
#include "base/main.h"

#ifdef DYNAMIC_MODULES
	#include "backends/plugins/dos/dos-provider.h"
#endif

int main(int argc, char *argv[]) {
	// SDL's DOS video and audio drivers reach the screen and the DMA buffers
	// through the flat data segment (SDL_main.h does this when it provides main())
	__djgpp_nearptr_enable();

	g_system = new OSystem_SDL_DOS();
	assert(g_system);

	// Pre initialize the backend
	g_system->init();

	#ifdef DYNAMIC_MODULES
	// Engines are DXE3 plug-ins in the PLUGINS folder
	PluginManager::instance().addPluginProvider(new DOSPluginProvider());
	#endif

	// Invoke the actual ScummVM main entry point:
	int res = scummvm_main(argc, argv);

	// Free OSystem
	g_system->destroy();

	// Leaving the launcher comes here rather than through OSystem_SDL_DOS::quit()
	dosNoMouseDriverReminder();

	return res;
}

#endif
