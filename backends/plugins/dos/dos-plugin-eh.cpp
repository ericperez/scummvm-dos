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

// Linked into every DOS plug-in (DXE3 module) by backends/platform/dos/dxe-link.sh, not into the program.
//
// DJGPP's dlopen() does not register a module's exception unwind tables, so an exception thrown in a
// plug-in would terminate the program. This registers them (marked by dxe.ld with ___EH_FRAME_BEGIN__)
// before the module's constructors run, and deregisters them after its destructors.

extern "C" {
	extern char __EH_FRAME_BEGIN__[];
	void __register_frame_info(const void *begin, void *object);
	void *__deregister_frame_info(const void *begin);
}

namespace {

// The runtime's bookkeeping for the registered tables (the size of libgcc's "struct object")
char g_unwindObject[64];

void registerUnwindTables() __attribute__((constructor(101)));
void deregisterUnwindTables() __attribute__((destructor(101)));

void registerUnwindTables() {
	__register_frame_info(__EH_FRAME_BEGIN__, g_unwindObject);
}

void deregisterUnwindTables() {
	__deregister_frame_info(__EH_FRAME_BEGIN__);
}

} // End of anonymous namespace
