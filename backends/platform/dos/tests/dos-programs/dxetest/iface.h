// What the host program shares with its module: the same kinds of things ScummVM and an engine plug-in share.
#ifndef DXETEST_IFACE_H
#define DXETEST_IFACE_H

/** A class the module implements and the host calls through its vtable (ScummVM's Engine / MetaEngine) */
struct Plugin {
	virtual ~Plugin() {}
	virtual const char *name() const = 0;
	virtual int run(int x) = 0;
};

/** An exception type the host throws and the module catches */
struct HostError {
	int code;
};

// A global of the host that the module changes, and a function of the host the module calls

extern int g_hostCounter;
void hostLog(const char *text);

#endif
