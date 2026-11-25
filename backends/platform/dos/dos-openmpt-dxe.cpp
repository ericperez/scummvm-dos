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

// Linked with libopenmpt into OPENMPT.DXE (dos.mk), not into the program

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "common/scummsys.h"

#include "backends/platform/dos/dos-openmpt.h"

#ifdef DOS_OPENMPT_DXE

const DosOpenMPTApi *dosOpenMPTGetApi() {
	static const DosOpenMPTApi api = {
		kDosOpenMPTApiVersion,
		openmpt_module_create2,
		openmpt_module_destroy,
		openmpt_module_read_interleaved_stereo,
		openmpt_module_set_position_seconds,
		openmpt_error_string,
		openmpt_free_string
	};
	return &api;
}

#endif
