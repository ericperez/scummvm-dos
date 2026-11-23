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

#ifndef BACKENDS_PLATFORM_DOS_FILESYS_H
#define BACKENDS_PLATFORM_DOS_FILESYS_H

#include "common/archive.h"
#include "common/fs.h"
#include "common/str.h"

#include "backends/saves/default/default-saves.h"
#include "backends/platform/dos/dos-names.h"

// The parts of dos-files.cpp that make no DOS calls, so unit tested on the host

inline bool dosNeedsShortName(const Common::String &name) {
	if (name.empty())
		return false;
	return dosShortName(name) != name;
}

/** Finds a file asked for by a long name under the 8.3 name DOS gave it */
class DOSShortNameArchive : public Common::Archive {
public:
	explicit DOSShortNameArchive(const Common::FSNode &folder) : _folder(folder) {}

	bool hasFile(const Common::Path &path) const override {
		Common::FSNode node;
		return find(path, node);
	}

	int listMatchingMembers(Common::ArchiveMemberList &list, const Common::Path &pattern, bool matchPathComponents = false) const override {
		// Nothing can be listed under a long name; but a name given outright can be found
		Common::String name = pattern.toString('/');
		if (name.findFirstOf("*?#") != Common::String::npos)
			return 0;

		Common::FSNode node;
		if (!find(pattern, node))
			return 0;
		list.push_back(Common::ArchiveMemberPtr(new Common::FSNode(node)));
		return 1;
	}

	int listMembers(Common::ArchiveMemberList &list) const override {
		return 0;
	}

	const Common::ArchiveMemberPtr getMember(const Common::Path &path) const override {
		Common::FSNode node;
		if (!find(path, node))
			return Common::ArchiveMemberPtr();
		return Common::ArchiveMemberPtr(new Common::FSNode(node));
	}

	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &path) const override {
		Common::FSNode node;
		if (!find(path, node))
			return nullptr;
		return node.createReadStream();
	}

private:
	bool find(const Common::Path &path, Common::FSNode &node) const {
		Common::String name = path.toString('/');
		if (name.contains('/') || !dosNeedsShortName(name))
			return false;

		// Only a name with one dot, not leading, has a short form that is the same file;
		// this also spares a file system call for each Mac resource fork lookup
		if (name[0] == '.' || name.findFirstOf('.') != name.findLastOf('.'))
			return false;

		Common::FSNode child = _folder.getChild(dosShortName(name));
		if (!child.exists() || child.isDirectory() || !child.isReadable())
			return false;
		node = child;
		return true;
	}

	Common::FSNode _folder;
};

/** Savegame names the disk can hold, whatever the name asked for (see dosSaveAliases). */
class DOSSaveFileManager : public DefaultSaveFileManager {
public:
	/** @param longNames says whether the system can hold long file names (a long file name driver is loaded) */
	explicit DOSSaveFileManager(bool (*longNames)()) : _longNames(longNames) {}

	Common::StringArray listSavefiles(const Common::String &pattern) override {
		Common::StringArray result = DefaultSaveFileManager::listSavefiles(pattern);

		// For a fixed base ("bladerunner-win.###"), list its 8.3 aliases (blader~1.000) by long name
		uint dot = pattern.findLastOf('.');
		if (dot == Common::String::npos)
			return result;
		Common::String base(pattern.c_str(), dot);
		if (base.findFirstOf("*?#\\") != Common::String::npos)
			return result;

		Common::StringArray all = DefaultSaveFileManager::listSavefiles("*");
		for (const auto &file : all) {
			uint fdot = file.findLastOf('.');
			if (fdot == Common::String::npos)
				continue;
			Common::String longName = base + file.substr(fdot);
			if (dosNameFits83(longName) || !longName.matchString(pattern, true) || contains(result, longName))
				continue;

			Common::Array<Common::String> aliases;
			dosSaveAliases(longName, aliases);
			if (contains(aliases, file))
				result.push_back(longName);
		}
		return result;
	}

	Common::InSaveFile *openForLoading(const Common::String &name) override {
		return DefaultSaveFileManager::openForLoading(existingName(name));
	}

	Common::InSaveFile *openRawFile(const Common::String &name) override {
		return DefaultSaveFileManager::openRawFile(existingName(name));
	}

	Common::OutSaveFile *openForSaving(const Common::String &name, bool compress = true) override {
		Common::String onDisk = existingName(name);
		if (onDisk == name && !dosNameFits83(name) && !_longNames()) {
			// No long names here: take the name that a system with long names will find too
			Common::Array<Common::String> aliases;
			dosSaveAliases(name, aliases);
			onDisk = aliases[0];
		}
		return DefaultSaveFileManager::openForSaving(onDisk, compress);
	}

	bool removeSavefile(const Common::String &name) override {
		return DefaultSaveFileManager::removeSavefile(existingName(name));
	}

	bool exists(const Common::String &name) override {
		return DefaultSaveFileManager::exists(existingName(name));
	}

private:
	bool (*_longNames)();

	static bool contains(const Common::Array<Common::String> &list, const Common::String &name) {
		for (const auto &item : list)
			if (item.equalsIgnoreCase(name))
				return true;
		return false;
	}

	/** The name of the file that holds the save called `name`, or `name` if there is none yet */
	Common::String existingName(const Common::String &name) {
		if (dosNameFits83(name) || DefaultSaveFileManager::exists(name))
			return name;

		Common::Array<Common::String> aliases;
		dosSaveAliases(name, aliases);
		for (const auto &alias : aliases)
			if (DefaultSaveFileManager::exists(alias))
				return alias;
		return name;
	}
};

#endif
