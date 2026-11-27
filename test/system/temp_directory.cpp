#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "temp_directory.h"

#ifdef POSIX

#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

Common::String Common::make_temp_directory() {
	const char *base = getenv("TMPDIR");
	Common::String templ(base && *base ? base : "/tmp");
	templ += "/scummvm-test-XXXXXX";

	char *buffer = strdup(templ.c_str());
	char *made = mkdtemp(buffer);
	Common::String path(made ? made : "");
	free(buffer);
	return path;
}

void Common::remove_temp_directory(const Common::String &path) {
	if (path.empty())
		return;

	DIR *dir = opendir(path.c_str());
	if (dir) {
		while (struct dirent *entry = readdir(dir)) {
			Common::String name(entry->d_name);
			if (name != "." && name != "..")
				unlink((path + "/" + name).c_str());
		}
		closedir(dir);
	}
	rmdir(path.c_str());
}

Common::String Common::change_current_directory(const Common::String &path) {
	char buffer[4096];
	Common::String previous(getcwd(buffer, sizeof(buffer)) ? buffer : "");
	if (chdir(path.c_str()) != 0)
		return Common::String();
	return previous;
}

#endif
