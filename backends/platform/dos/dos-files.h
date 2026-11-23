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

#ifndef BACKENDS_PLATFORM_DOS_FILES_H
#define BACKENDS_PLATFORM_DOS_FILES_H

#include "common/archive.h"
#include "common/str.h"

#include "backends/platform/dos/dos-names.h"

/**
 * Add the current folder and its DATA folder, where the data files ScummVM ships
 * (kyra.dat, translations.dat...) are also found by their 8.3 names (TRANSLAT.DAT)
 * when there is no long file name driver.
 */
void dosAddDataFolders(Common::SearchSet &set, int priority);

class DefaultSaveFileManager;

/**
 * The savegame manager. Names that do not fit 8.3 are aliased (dosSaveAliases in
 * dos-names.h), so saves work with or without a long file name driver.
 */
DefaultSaveFileManager *dosCreateSaveFileManager();

/** A language code such as "de_DE" from the country set in CONFIG.SYS (en_US if unknown). */
Common::String dosSystemLanguage();

#endif
