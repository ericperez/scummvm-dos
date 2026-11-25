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

#ifndef BACKENDS_PLATFORM_DOS_CDAUDIO_H
#define BACKENDS_PLATFORM_DOS_CDAUDIO_H

class AudioCDManager;

/**
 * Audio CD tracks through MSCDEX, or from track files (track2.ogg...) when the game
 * has them, as on other systems. Without MSCDEX or a drive, only the files are used.
 */
AudioCDManager *dosCreateAudioCDManager();

/**
 * Stop the drive and put back the sound card's CD input if it was raised, for a program that
 * leaves through _exit(), past the CD manager's destructor.
 */
void dosCdShutdown();

/**
 * Testing aid (SCUMMVM_DOS_CDTEST=drive|nodrive): logs "CDTEST:" lines, ending in "CDTEST: OK
 * <n> checks". "drive" expects the CD tests/gen-test-cd.py writes and an SB16 in BLASTER,
 * "nodrive" a track2.wav file.
 */
void dosCdSelfTest(const char *mode);

#endif
