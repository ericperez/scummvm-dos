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

#ifndef BACKENDS_PLUGINS_DOS_PROVIDER_H
#define BACKENDS_PLUGINS_DOS_PROVIDER_H

#include "base/plugins.h"

#if defined(DYNAMIC_MODULES) && defined(USE_DOS_DRIVER)

/** DXE3 plug-ins (DJGPP's dlopen) in the PLUGINS folder: see backends/platform/dos/PLUGINS.md. */
class DOSPluginProvider : public FilePluginProvider {
protected:
	Plugin *createPlugin(const Common::FSNode &node) const override;

	/** Matches the .dxe suffix in either case, as DOS may list names in capitals. */
	bool isPluginFilename(const Common::FSNode &node) const override;

	/** The PLUGINS folder in the folder the program runs in. */
	void addCustomDirectories(Common::FSList &dirs) const override;
};

/**
 * Testing aid (SCUMMVM_DOS_PLUGINTEST="id,id,..."): load and unload each engine's plug-in three times,
 * logging time and memory, ending in "PLUGINTEST: OK <n> checks". Needs the uncached plug-in manager.
 */
void dosPluginSelfTest(const char *engineIds);

#endif // defined(DYNAMIC_MODULES) && defined(USE_DOS_DRIVER)

#endif
