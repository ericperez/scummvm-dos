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

#if defined(USE_DOS_DRIVER)

#include <dpmi.h>
#include <go32.h>

#include "common/config-manager.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/path.h"
#include "common/textconsole.h"

#include "common/savefile.h"
#include "common/stream.h"
#include "common/system.h"

#include "backends/saves/default/default-saves.h"
#include "backends/platform/dos/dos-files.h"
#include "backends/platform/dos/dos-filesys.h"

extern "C" int _use_lfn(const char *path);

namespace {

bool longFileNames() {
	return _use_lfn(".") != 0;
}

} // End of anonymous namespace

DefaultSaveFileManager *dosCreateSaveFileManager() {
	return new DOSSaveFileManager(longFileNames);
}

void dosAddDataFolders(Common::SearchSet &set, int priority) {
	// The current folder (normally the program's), and DATA inside it for tidier installs
	static const char *const folders[] = { ".", "DATA" };

	for (uint i = 0; i < ARRAYSIZE(folders); i++) {
		Common::FSNode folder(folders[i]);
		if (!folder.exists() || !folder.isDirectory())
			continue;

		// By their long names (with a long file name driver) and by their 8.3 ones
		set.addDirectory(folders[i], folder, priority, 1, false);
		set.add(Common::String("dos-short-names-") + folders[i], new DOSShortNameArchive(folder), priority - 1, true);
	}
}

Common::String dosSystemLanguage() {
	// INT 21h, AX=3800h: country information. The country code comes back in BX.
	__dpmi_regs r;
	r.x.ax = 0x3800;
	r.x.ds = __tb >> 4;
	r.x.dx = __tb & 15;
	if (__dpmi_int(0x21, &r) != 0 || (r.x.flags & 1))
		return "en_US";

	return dosLanguageForCountry(r.x.bx);
}

#endif // USE_DOS_DRIVER
