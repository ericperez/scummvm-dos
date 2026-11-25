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

// DXE3 plug-ins, loaded by DJGPP's dlopen() into heap memory; their unresolved symbols come from the
// program's export table (backends/platform/dos/dos-exports.c, made by dos.mk)

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/scummsys.h"

#if defined(DYNAMIC_MODULES) && defined(USE_DOS_DRIVER)

#include <dlfcn.h>
#include <time.h>

#include "backends/platform/dos/dos-openmpt.h"
#include "backends/plugins/dos/dos-provider.h"
#include "backends/plugins/dynamic-plugin.h"
#include "common/config-manager.h"
#include "common/fs.h"
#include "common/textconsole.h"

#include <stdlib.h>
#include <string.h>

// DJGPP's mallinfo(), declared only in libc/malldbg.h: the same layout, under a name of its own
extern "C" {
	struct DosMallinfo {
		int arena;    ///< total space being managed
		int ordblks;
		int smblks;
		int hblks;
		int hblkhd;
		int usmblks;
		int fsmblks;
		int uordblks; ///< heap space in use
		int fordblks; ///< space in the free lists
		int keepcost;
	};
	struct DosMallinfo mallinfo(void);
}

namespace {

// SCUMMVM_DOS_PLUGLOG=1: say in the log what each plug-in takes to load, and what it gives back
int g_logState = -1;

bool pluginLogging() {
	if (g_logState < 0)
		g_logState = getenv("SCUMMVM_DOS_PLUGLOG") ? 1 : 0;
	return g_logState == 1;
}

unsigned heapInUse() {
	struct DosMallinfo info = mallinfo();
	return (unsigned)info.uordblks;
}

unsigned millisSince(uclock_t begin) {
	return (unsigned)((uclock() - begin) * 1000 / UCLOCKS_PER_SEC);
}

class DOSPlugin : public DynamicPlugin {
protected:
	void *_dlHandle;
	unsigned _heapBefore;

	VoidFunc findSymbol(const char *symbol) override {
		// The name without DJGPP's leading underscore first, then with it
		void *func = dlsym(_dlHandle, symbol);
		if (!func) {
			Common::String prefixed = Common::String("_") + symbol;
			func = dlsym(_dlHandle, prefixed.c_str());
		}
		if (!func)
			warning("Failed loading symbol '%s' from plugin '%s' (%s)", symbol, _filename.toString(Common::Path::kNativeSeparator).c_str(), dlerror());

		// See posix-provider.cpp: a function pointer from dlsym's void pointer
		assert(sizeof(VoidFunc) == sizeof(func));
		VoidFunc tmp;
		memcpy(&tmp, &func, sizeof(VoidFunc));
		return tmp;
	}

public:
	DOSPlugin(const Common::Path &filename) : DynamicPlugin(filename), _dlHandle(nullptr), _heapBefore(0) {}

	bool loadPlugin() override {
		assert(!_dlHandle);
		const Common::String path = _filename.toString(Common::Path::kNativeSeparator);
		const uclock_t begin = uclock();
		_heapBefore = heapInUse();

		_dlHandle = dlopen(path.c_str(), RTLD_LAZY);
		if (!_dlHandle) {
			warning("Failed loading plugin '%s' (%s)", path.c_str(), dlerror());
			return false;
		}

		if (pluginLogging())
			warning("DOSPLUGIN: dlopen of %s done, asking it for its plug-in object", _filename.baseName().c_str());

		if (!DynamicPlugin::loadPlugin()) {
			unloadPlugin();
			return false;
		}

		if (pluginLogging())
			warning("DOSPLUGIN: loaded %s in %u ms, %u KB of memory", _filename.baseName().c_str(), millisSince(begin),
			        (heapInUse() - _heapBefore) / 1024);
		return true;
	}

	void unloadPlugin() override {
		const uclock_t begin = uclock();
		const unsigned heapLoaded = heapInUse();

		DynamicPlugin::unloadPlugin();
		if (_dlHandle) {
			if (dlclose(_dlHandle) != 0)
				warning("Failed unloading plugin '%s' (%s)", _filename.toString(Common::Path::kNativeSeparator).c_str(), dlerror());
			_dlHandle = nullptr;

			if (pluginLogging())
				warning("DOSPLUGIN: unloaded %s in %u ms, %d KB of memory given back", _filename.baseName().c_str(),
				        millisSince(begin), (int)((int)heapLoaded - (int)heapInUse()) / 1024);
		}

		// An engine with tracker music leaves with its plug-in
		dosOpenMPTRelease();
	}
};

} // End of anonymous namespace

unsigned dosHeapInUse() {
	return heapInUse();
}

Plugin *DOSPluginProvider::createPlugin(const Common::FSNode &node) const {
	if (pluginLogging())
		warning("DOSPLUGIN: found %s", node.getName().c_str());
	return new DOSPlugin(node.getPath());
}

bool DOSPluginProvider::isPluginFilename(const Common::FSNode &node) const {
	Common::String name = node.getName();
	// Tracker music (dos-openmpt.cpp), in the program's folder, which is searched for plug-ins too
	if (name.equalsIgnoreCase(kDosOpenMPTFile))
		return false;
	name.toLowercase();
	#ifdef PLUGIN_SUFFIX
	return name.hasSuffix(PLUGIN_SUFFIX);
	#else
	return true;
	#endif
}

void DOSPluginProvider::addCustomDirectories(Common::FSList &dirs) const {
	// Like the data files, relative to the folder the program is run in
	dirs.push_back(Common::FSNode("PLUGINS"));
}

// ---- The self-test ----

namespace {

struct PluginChecks {
	int count;
	bool failed;
	PluginChecks() : count(0), failed(false) {}

	void check(bool condition, const char *what, const Common::String &engine) {
		count++;
		if (!condition) {
			failed = true;
			warning("PLUGINTEST: FAILED %s: %s", engine.c_str(), what);
		}
	}
};

} // End of anonymous namespace

void dosPluginSelfTest(const char *engineIds) {
	PluginChecks c;
	PluginManager &pm = PluginMan;

	// The memory the test started from: after the first round of everything, so that the one-time costs
	// (directory listings, caches) are not mistaken for a leak
	unsigned baseline = 0;
	int engines = 0;

	#ifndef DETECTION_STATIC
	// The launcher has loaded the detection plug-in already: start from unloaded
	pm.unloadDetectionPlugin();
	for (int round = 0; round < 3; round++) {
		const unsigned before = heapInUse();
		const uclock_t begin = uclock();
		pm.loadDetectionPlugin();
		const unsigned loadMs = millisSince(begin);
		const unsigned loaded = heapInUse();
		const unsigned detectionPlugins = pm.getPlugins(PLUGIN_TYPE_ENGINE_DETECTION).size();
		c.check(detectionPlugins > 0, "the detection plug-in gives engine detection plug-ins", "detection");

		const uclock_t unloadBegin = uclock();
		pm.unloadDetectionPlugin();
		const unsigned unloadMs = millisSince(unloadBegin);
		c.check(pm.getPlugins(PLUGIN_TYPE_ENGINE_DETECTION).empty(), "no engine detection plug-ins are left after unloading", "detection");

		warning("PLUGINTEST: detection round %d: %u detection plug-ins, load %u ms (%u KB), unload %u ms, %d KB not given back",
		        round + 1, detectionPlugins, loadMs, (loaded - before) / 1024, unloadMs, (int)(heapInUse() - before) / 1024);
		if (round == 0)
			baseline = heapInUse();
	}
	c.check((int)(heapInUse() - baseline) < 256 * 1024, "loading and unloading the detection plug-in does not leak", "detection");
	#endif

	Common::String list(engineIds);

	// "all": every engine plug-in file in the PLUGINS folder, one round each, by its file name (the name is cut to
	// 8 characters like the file's, so it finds that file)
	const bool all = list == "all";
	if (all) {
		list.clear();
		Common::FSList files;
		Common::FSNode("PLUGINS").getChildren(files, Common::FSNode::kListFilesOnly);
		for (const auto &file : files) {
			Common::String name = file.getName();
			name.toLowercase();
			if (!name.hasSuffix(".dxe"))
				continue;
			name = name.substr(0, name.size() - 4);
			if (name == "detectio")
				continue;
			list += (list.empty() ? "" : ",") + name;
		}
	}
	const int rounds = all ? 1 : 3;

	while (!list.empty()) {
		const uint comma = list.findFirstOf(',');
		const Common::String id = list.substr(0, comma == Common::String::npos ? list.size() : comma);
		list = comma == Common::String::npos ? Common::String() : list.substr(comma + 1);
		engines++;

		unsigned engineBaseline = 0;
		for (int round = 0; round < rounds; round++) {
			const unsigned before = heapInUse();
			const uclock_t begin = uclock();
			const bool loaded = pm.loadPluginFromEngineId(id);
			const unsigned loadMs = millisSince(begin);
			const unsigned used = heapInUse() - before;
			c.check(loaded, "the engine plug-in is found by its id and loads", id);
			if (!loaded)
				break;

			c.check(pm.getPlugins(PLUGIN_TYPE_ENGINE).size() == 1, "exactly one engine plug-in is in memory", id);
			if (pm.getPlugins(PLUGIN_TYPE_ENGINE).size() == 1) {
				const Plugin *plugin = pm.getPlugins(PLUGIN_TYPE_ENGINE)[0];
				c.check(plugin->getType() == PLUGIN_TYPE_ENGINE, "it is an engine plug-in", id);
			}

			const uclock_t unloadBegin = uclock();
			pm.unloadPluginsExcept(PLUGIN_TYPE_ENGINE, nullptr, false);
			const unsigned unloadMs = millisSince(unloadBegin);
			c.check(pm.getPlugins(PLUGIN_TYPE_ENGINE).empty(), "no engine plug-in is in memory after unloading", id);

			const int left = (int)(heapInUse() - before);
			warning("PLUGINTEST: %s round %d: load %u ms, %u KB in use, unload %u ms, %d KB not given back", id.c_str(),
			        round + 1, loadMs, used / 1024, unloadMs, left / 1024);
			if (round == 0)
				engineBaseline = heapInUse();
		}
		if (rounds > 1)
			c.check((int)(heapInUse() - engineBaseline) < 256 * 1024, "loading and unloading again does not leak", id);
	}

	// Over the whole run the heap must not have grown by more than a little (the first load of each engine may keep
	// a few things, like a directory listing)
	if (all)
		c.check((int)(heapInUse() - baseline) < 2 * 1024 * 1024, "loading and unloading every plug-in once leaves the heap where it was", "all");

	if (c.failed)
		warning("PLUGINTEST: FAILED (%d checks)", c.count);
	else
		warning("PLUGINTEST: OK %d checks, %d engines", c.count, engines);
}

#endif // defined(DYNAMIC_MODULES) && defined(USE_DOS_DRIVER)
