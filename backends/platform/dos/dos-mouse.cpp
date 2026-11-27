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
#include <stdio.h>
#include <string.h>
#include <sys/movedata.h>

#include "common/endian.h"
#include "common/textconsole.h"

#include "backends/platform/dos/dos-mouse.h"

static const char *const kNoMouseDriver = "no mouse driver is loaded: Load one, such as CTMOUSE or MOUSE, before starting ScummVM or the mouse will not work.";
static bool g_noMouseDriver = false;

// Whether a mouse driver answers on INT 33h
static bool mouseDriverPresent() {
	// Old DOS versions leave unused vectors at 0000:0000, which would hang the call
	byte entry[4];
	dosmemget(0x33 * 4, sizeof(entry), entry);
	if (READ_LE_UINT16(entry) == 0 && READ_LE_UINT16(entry + 2) == 0)
		return false;

	// Function 0 (reset) answers AX = FFFFh when a driver is installed
	__dpmi_regs r;
	memset(&r, 0, sizeof(r));
	__dpmi_int(0x33, &r);
	return r.x.ax == 0xFFFF;
}

void dosCheckMouseDriver() {
	g_noMouseDriver = !mouseDriverPresent();
	if (g_noMouseDriver)
		warning("DOS: %s", kNoMouseDriver);
}

void dosNoMouseDriverReminder() {
	if (g_noMouseDriver)
		fprintf(stderr, "ScummVM: %s.\n", kNoMouseDriver);
}

#endif // USE_DOS_DRIVER
