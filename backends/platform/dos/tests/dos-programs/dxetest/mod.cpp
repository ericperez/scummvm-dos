// The module (a stand-in for an engine plug-in). Built into MOD.DXE by dxe-link.sh, as a real plug-in is:
// only the PLUGIN_ names are exported, and the unwind tables are registered by the object the script adds.
#include <stdio.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "iface.h"


static int g_ctorRan = 0;

// A global object with a constructor and destructor, as every ScummVM engine has (its plug-in registration)
struct StaticInit {
	StaticInit() {
		g_ctorRan = 42;
		g_hostCounter++;
	}
	~StaticInit() {
		hostLog("the module's global destructor ran");
	}
};
static StaticInit g_staticInit;

struct ModPlugin : Plugin {
	const char *name() const override {
		return "mod";
	}

	int run(int x) override {
		// libstdc++ (the host has to supply it to the module)
		std::string s = "abc";
		s += std::to_string(x);
		std::vector<int> v;
		for (int i = 0; i < x; i++)
			v.push_back(i);

		// An exception thrown and caught inside the module, and one thrown by the host and caught here
		int caught = 0;
		try {
			throw std::runtime_error("inside the module");
		} catch (const std::exception &e) {
			caught += 1;
		}
		return (int)s.size() + (int)v.size() + caught * 100 + g_ctorRan;
	}
};

extern "C" Plugin *PLUGIN_makePlugin() {
	return new ModPlugin;
}

// Throws from module code into the host
extern "C" void PLUGIN_throwToHost() {
	throw std::runtime_error("thrown by the module");
}

// Catches a HostError the host throws through it
extern "C" int PLUGIN_catchFromHost(void (*host)()) {
	try {
		host();
	} catch (const HostError &e) {
		return e.code;
	}
	return -1;
}
