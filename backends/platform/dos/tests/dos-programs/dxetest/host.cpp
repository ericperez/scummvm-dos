// The host program: loads MOD.DXE with dlopen (DJGPP's DXE3 loader) and uses it the way ScummVM uses an
// engine plug-in. Prints one DXETEST line a check and "DXETEST: OK <n> checks" at the end.
#include <dlfcn.h>
#include <stdio.h>
#include <stdexcept>
#include <string.h>

#include "iface.h"

int g_hostCounter = 0;

void hostLog(const char *text) {
	printf("host: %s\n", text);
	fflush(stdout);
}

static int g_checks = 0, g_failed = 0;

static void check(bool ok, const char *what) {
	g_checks++;
	printf("DXETEST: %s: %s\n", ok ? "ok" : "FAILED", what);
	if (!ok)
		g_failed++;
}

static void hostThrower() {
	throw HostError{7};
}

typedef Plugin *(*MakeFn)();
typedef void (*ThrowFn)();
typedef int (*CatchFn)(void (*)());

// DJGPP symbols have a leading underscore at the object level: try the name as it is, then with one
static void *lookup(void *handle, const char *name) {
	void *p = dlsym(handle, name);
	if (!p) {
		char with[128];
		snprintf(with, sizeof(with), "_%s", name);
		p = dlsym(handle, with);
	}
	return p;
}

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, 0);
	const char *file = argc > 1 ? argv[1] : "MOD.DXE";

	void *handle = dlopen(file, RTLD_LAZY);
	if (!handle) {
		printf("DXETEST: FAILED dlopen(%s): %s\n", file, dlerror());
		return 1;
	}
	check(true, "dlopen");
	check(g_hostCounter == 1, "the module's global constructor ran at load and reached a host global");

	MakeFn make = (MakeFn)lookup(handle, "PLUGIN_makePlugin");
	check(make != 0, "dlsym finds an extern \"C\" function");
	if (!make)
		return 1;

	Plugin *p = make();
	check(p != 0 && strcmp(p->name(), "mod") == 0, "a virtual call into the module");
	// run(5): "abc5" is 4, five items in the vector, one exception caught (100), the constructor's 42
	check(p->run(5) == 4 + 5 + 100 + 42, "the module uses std::string, std::vector and its own try/catch");
	check(dynamic_cast<Plugin *>(p) != 0, "dynamic_cast to the host's class");

	ThrowFn thrower = (ThrowFn)lookup(handle, "PLUGIN_throwToHost");
	bool caught = false;
	if (thrower) {
		try {
			thrower();
		} catch (const std::runtime_error &e) {
			caught = strcmp(e.what(), "thrown by the module") == 0;
		}
	}
	check(caught, "an exception thrown in the module is caught by the host");

	CatchFn catcher = (CatchFn)lookup(handle, "PLUGIN_catchFromHost");
	check(catcher && catcher(hostThrower) == 7, "an exception thrown by the host is caught in the module");

	delete p; // the virtual destructor, and operator delete
	check(true, "delete of an object made in the module");

	check(dlclose(handle) == 0, "dlclose (the module's global destructor should have said so above)");

	if (g_failed)
		printf("DXETEST: FAILED %d of %d checks\n", g_failed, g_checks);
	else
		printf("DXETEST: OK %d checks\n", g_checks);
	return g_failed ? 1 : 0;
}
