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

// 8.3 file names and CONFIG.SYS country codes; no DOS calls, so unit tested on the host

#ifndef BACKENDS_PLATFORM_DOS_NAMES_H
#define BACKENDS_PLATFORM_DOS_NAMES_H

#include "common/str.h"
#include "common/array.h"

/** The 8.3 name DOS gives a file: the base cut to 8 characters, the extension to 3 */
inline Common::String dosShortName(const Common::String &name) {
	uint dot = name.findFirstOf('.');
	if (dot == Common::String::npos)
		return Common::String(name.c_str(), MIN<uint>(8, name.size()));

	Common::String base(name.c_str(), MIN<uint>(8, dot));
	Common::String ext;
	for (uint i = dot + 1; i < name.size() && name[i] != '.' && ext.size() < 3; i++)
		ext += name[i];
	return ext.empty() ? base : base + "." + ext;
}

/** Does the name already fit 8.3? */
inline bool dosNameFits83(const Common::String &name) {
	if (name.empty())
		return false;
	uint dot = name.findFirstOf('.');
	if (dot == Common::String::npos)
		return name.size() <= 8;
	if (dot == 0 || dot > 8 || name.size() - dot - 1 > 3 || name.findLastOf('.') != dot)
		return false;
	return true;
}

/**
 * The names a save can have on an 8.3 disk, in lookup order: this port's own
 * hashed name ("blade3f2.000"), the Windows/LFN aliases ("blader~1.000" to
 * "blader~9.000"), and DJGPP's plain cut ("bladerun.000").
 */
inline void dosSaveAliases(const Common::String &name, Common::Array<Common::String> &out) {
	out.clear();
	uint dot = name.findLastOf('.');
	Common::String base = dot == Common::String::npos ? name : Common::String(name.c_str(), dot);
	Common::String ext = dot == Common::String::npos ? Common::String() : Common::String(name.c_str() + dot + 1).substr(0, 3);

	// Lower case, without the characters 8.3 cannot hold
	Common::String clean;
	for (uint i = 0; i < base.size(); i++) {
		char c = base[i];
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '$' || c == '!' || c == '#' || c == '&' || c == '@')
			clean += c;
	}
	if (clean.empty())
		clean = "save";
	Common::String suffix = ext.empty() ? Common::String() : "." + ext;

	// FNV-1a of the lower case name
	uint32 h = 2166136261u;
	for (uint i = 0; i < name.size(); i++) {
		char c = name[i];
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
		h = (h ^ (byte)c) * 16777619u;
	}
	static const char hex[] = "0123456789abcdef";
	Common::String hashName = clean.substr(0, 5);
	hashName += hex[(h >> 8) & 15];
	hashName += hex[(h >> 4) & 15];
	hashName += hex[h & 15];
	out.push_back(hashName + suffix);

	for (int n = 1; n <= 9; n++)
		out.push_back(clean.substr(0, 6) + Common::String::format("~%d", n) + suffix);

	out.push_back(clean.substr(0, 8) + suffix);
}

/** The ScummVM language for a CONFIG.SYS country code; "en_US" if unknown */
inline const char *dosLanguageForCountry(unsigned country) {
	switch (country) {
	case 7:
		return "ru_RU";
	case 20:
		return "ar_EG";
	case 966:
		return "ar_SA";
	case 971:
		return "ar_AE";
	case 972:
		return "he_IL";
	case 30:
		return "el_GR";
	case 31:
		return "nl_NL";
	case 32:
		return "nl_BE";
	case 33:
		return "fr_FR";
	case 34:
		return "es_ES";
	case 36:
		return "hu_HU";
	case 39:
		return "it_IT";
	case 41:
		return "de_CH";
	case 43:
		return "de_AT";
	case 44:
		return "en_GB";
	case 45:
		return "da_DK";
	case 46:
		return "sv_SE";
	case 47:
		return "nb_NO";
	case 48:
		return "pl_PL";
	case 49:
		return "de_DE";
	case 55:
		return "pt_BR";
	case 90:
		return "tr_TR";
	case 351:
		return "pt_PT";
	case 358:
		return "fi_FI";
	case 380:
		return "uk_UA";
	case 420:
		return "cs_CZ";
	case 421:
		return "sk_SK";
	default:
		return "en_US";
	}
}

#endif
