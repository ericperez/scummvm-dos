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

// MSCDEX CD audio arithmetic and request layout for dos-cdaudio.cpp; no DOS calls, so unit
// tested on the host. A CD has 75 frames a second; a Red Book address packs minutes, seconds
// and frames (frame in the lowest byte), counted from a 2 second lead-in.

#ifndef BACKENDS_PLATFORM_DOS_CD_H
#define BACKENDS_PLATFORM_DOS_CD_H

#include "common/endian.h"
#include "common/scummsys.h"

#include "backends/platform/dos/dos-blaster.h"

/** The units of a CD's time line */
enum {
	kDosCdFramesPerSecond = 75,
	kDosCdFramesPerMinute = 75 * 60,
	kDosCdLeadIn = 150,          ///< the frames before 00:02:00, where HSG sector 0 is
	kDosCdMaxTrack = 99
};

// ---- Red Book addresses ----

/** The Red Book address (minute, second and frame, frame in the lowest byte) of a frame count */
inline uint32 dosCdFramesToRedBook(uint32 frames) {
	const uint32 minute = frames / kDosCdFramesPerMinute;
	const uint32 second = (frames / kDosCdFramesPerSecond) % 60;
	const uint32 frame = frames % kDosCdFramesPerSecond;
	return frame | (second << 8) | (minute << 16);
}

/** The frame count of a Red Book address */
inline uint32 dosCdRedBookToFrames(uint32 address) {
	return (address & 0xFF) + ((address >> 8) & 0xFF) * kDosCdFramesPerSecond +
	       ((address >> 16) & 0xFF) * kDosCdFramesPerMinute;
}

/** The HSG sector of a frame counted as Red Book addresses count */
inline uint32 dosCdFramesToHsg(uint32 frames) {
	return frames >= kDosCdLeadIn ? frames - kDosCdLeadIn : 0;
}

/** How long a number of frames plays, in milliseconds */
inline uint32 dosCdFramesToMillis(uint32 frames) {
	return (uint32)((uint64)frames * 1000 / kDosCdFramesPerSecond);
}

// ---- The table of contents ----

/** The tracks of a disc, as the driver reports them */
struct DosCdToc {
	int first, last;                    ///< number of the first and last track (0 and 0: none read)
	uint32 leadOut;                     ///< where the disc ends, in frames
	uint32 start[kDosCdMaxTrack + 2];   ///< where each track starts, in frames, by track number
	bool data[kDosCdMaxTrack + 2];      ///< a data track, not music

	DosCdToc() : first(0), last(0), leadOut(0) {
		for (int i = 0; i < kDosCdMaxTrack + 2; i++) {
			start[i] = 0;
			data[i] = false;
		}
	}

	bool valid() const {
		return first >= 1 && last >= first && last <= kDosCdMaxTrack && leadOut > start[last];
	}

	/** How long a track is: up to the start of the next one, or the end of the disc */
	uint32 length(int track) const {
		if (track < first || track > last)
			return 0;
		const uint32 end = (track == last) ? leadOut : start[track + 1];
		return end > start[track] ? end - start[track] : 0;
	}

	bool hasMusic() const {
		if (!valid())
			return false;
		for (int track = first; track <= last; track++) {
			if (!data[track])
				return true;
		}
		return false;
	}
};

/** The part of the disc to play */
struct DosCdRange {
	uint32 start;     ///< frame of the disc to start at
	uint32 frames;    ///< how many frames
};

/**
 * The disc track for a ScummVM track, which counts from 0 as in the SDL CD manager (track 1
 * is the first music track on a disc that starts with data); -1 if there is none
 */
inline int dosCdPhysicalTrack(const DosCdToc &toc, int scummvmTrack) {
	if (!toc.valid() || scummvmTrack < 0 || scummvmTrack > toc.last - toc.first)
		return -1;
	return toc.first + scummvmTrack;
}

/**
 * The frames to play from startFrame frames into a disc track, for duration frames (0: to
 * its end), never past its end; false if the track is not music or the start is outside it
 */
inline bool dosCdTrackRange(const DosCdToc &toc, int track, int startFrame, int duration, DosCdRange &range) {
	if (!toc.valid() || track < toc.first || track > toc.last || toc.data[track])
		return false;
	if (startFrame < 0 || duration < 0)
		return false;

	const uint32 length = toc.length(track);
	if ((uint32)startFrame >= length)
		return false;

	uint32 frames = length - startFrame;
	if (duration > 0 && (uint32)duration < frames)
		frames = duration;

	range.start = toc.start[track] + startFrame;
	range.frames = frames;
	return true;
}

// ---- Requests to the CD-ROM device driver (INT 2Fh AX=1510h) ----
// A 13 byte header: length, subunit, command, 16 bit status, 8 reserved bytes

/** Sizes, commands and status bits of the requests to the device driver */
enum {
	kDosCdHeaderSize = 13,
	kDosCdCmdIoctlInput = 3,
	kDosCdCmdIoctlOutput = 12,
	kDosCdCmdPlay = 0x84,
	kDosCdCmdStop = 0x85,
	kDosCdCmdResume = 0x88,

	kDosCdPlayRequestSize = 22,
	kDosCdIoctlRequestSize = 26,

	// What an IOCTL request asks for: the first byte of its control block

	kDosCdIoctlAudioChannels = 4,    ///< input: the volume the drive plays at
	kDosCdIoctlDeviceStatus = 6,
	kDosCdIoctlDiscInfo = 10,
	kDosCdIoctlTrackInfo = 11,
	kDosCdIoctlAudioStatus = 15,
	kDosCdIoctlSetChannels = 3,      ///< output: set the volume

	// Status word the driver answers with

	kDosCdStatusError = 0x8000,
	kDosCdStatusDone = 0x0100,
	kDosCdStatusBusy = 0x0200        ///< audio is playing
};

/**
 * Clear a request and fill in its header.
 *
 * @param request  the request
 * @param length   its size in bytes, all of which are cleared
 * @param command  the command (kDosCdCmd*)
 */
inline void dosCdFillHeader(uint8 *request, int length, int command) {
	for (int i = 0; i < length; i++)
		request[i] = 0;
	request[0] = (uint8)length;
	request[2] = (uint8)command;
}

/**
 * Play audio from a frame of the disc (00:02:00 is frame 150): the 22 bytes of the request. The
 * start is an HSG sector, which every driver takes; Red Book addressing is optional.
 */
inline int dosCdBuildPlay(uint8 *request, uint32 startFrame, uint32 frames) {
	dosCdFillHeader(request, kDosCdPlayRequestSize, kDosCdCmdPlay);
	request[13] = 0;                                         // addressing mode: HSG
	WRITE_LE_UINT32(request + 14, dosCdFramesToHsg(startFrame));
	WRITE_LE_UINT32(request + 18, frames);
	return kDosCdPlayRequestSize;
}

/**
 * Stop (or pause) the audio that plays: the 13 bytes of the request.
 *
 * @return the size of the request
 */
inline int dosCdBuildStop(uint8 *request) {
	dosCdFillHeader(request, kDosCdHeaderSize, kDosCdCmdStop);
	return kDosCdHeaderSize;
}

/** An IOCTL request to read (input) or send (output) the control block at segment:offset */
inline int dosCdBuildIoctl(uint8 *request, bool output, uint16 segment, uint16 offset, uint16 bytes) {
	dosCdFillHeader(request, kDosCdIoctlRequestSize, output ? kDosCdCmdIoctlOutput : kDosCdCmdIoctlInput);
	request[13] = 0;                                         // media descriptor
	WRITE_LE_UINT16(request + 14, offset);                   // far pointer: offset, then segment
	WRITE_LE_UINT16(request + 16, segment);
	WRITE_LE_UINT16(request + 18, bytes);
	WRITE_LE_UINT16(request + 20, 0);                        // starting sector (not used)
	WRITE_LE_UINT32(request + 22, 0);                        // volume ID (not used)
	return kDosCdIoctlRequestSize;
}

/** The status word the driver put in a request it has carried out */
inline uint16 dosCdRequestStatus(const uint8 *request) {
	return READ_LE_UINT16(request + 3);
}

/** Does the status word say that the request failed? */
inline bool dosCdStatusFailed(uint16 status) {
	return (status & kDosCdStatusError) != 0;
}

/** Does the status word say that audio is playing? */
inline bool dosCdStatusPlaying(uint16 status) {
	return (status & kDosCdStatusBusy) != 0;
}

// ---- Control blocks ----

/** The sizes of the IOCTL control blocks, in bytes */
enum {
	kDosCdDeviceStatusSize = 5,
	kDosCdDiscInfoSize = 7,
	kDosCdTrackInfoSize = 7,
	kDosCdChannelsSize = 9,
	kDosCdAudioStatusSize = 11
};

/** Device status bits */
enum {
	kDosCdDevicePlaysAudio = 1 << 4,
	kDosCdDeviceSetsVolume = 1 << 8,
	kDosCdDeviceNoDisc = 1 << 11
};

/** Device status: what the driver can do, and whether there is a disc */
inline void dosCdBuildDeviceStatus(uint8 *block) {
	for (int i = 0; i < kDosCdDeviceStatusSize; i++)
		block[i] = 0;
	block[0] = kDosCdIoctlDeviceStatus;
}

/**
 * Read the answer to a device status question.
 *
 * @param block   the control block the driver filled in
 * @param status  set to the device status bits (kDosCdDevice*)
 * @return false if the block is the answer to another question
 */
inline bool dosCdParseDeviceStatus(const uint8 *block, uint32 &status) {
	if (block[0] != kDosCdIoctlDeviceStatus)
		return false;
	status = READ_LE_UINT32(block + 1);
	return true;
}

/** Audio status: all that is asked of it is the status word of the answer (busy: playing) */
inline void dosCdBuildAudioStatus(uint8 *block) {
	for (int i = 0; i < kDosCdAudioStatusSize; i++)
		block[i] = 0;
	block[0] = kDosCdIoctlAudioStatus;
}

/** Disc information: lowest and highest track, and where the disc ends */
inline void dosCdBuildDiscInfo(uint8 *block) {
	for (int i = 0; i < kDosCdDiscInfoSize; i++)
		block[i] = 0;
	block[0] = kDosCdIoctlDiscInfo;
}

/**
 * Read the answer to a disc information question into a table of contents.
 *
 * @param block  the control block the driver filled in
 * @param toc    its first and last tracks, and where the disc ends, are set
 * @return false if the answer is to another question or makes no sense
 */
inline bool dosCdParseDiscInfo(const uint8 *block, DosCdToc &toc) {
	if (block[0] != kDosCdIoctlDiscInfo || block[1] < 1 || block[2] < block[1] || block[2] > kDosCdMaxTrack)
		return false;
	toc.first = block[1];
	toc.last = block[2];
	toc.leadOut = dosCdRedBookToFrames(READ_LE_UINT32(block + 3));
	return true;
}

/** Track information: where one track starts, and whether it is data (bit 6 of its control byte) */
inline void dosCdBuildTrackInfo(uint8 *block, int track) {
	for (int i = 0; i < kDosCdTrackInfoSize; i++)
		block[i] = 0;
	block[0] = kDosCdIoctlTrackInfo;
	block[1] = (uint8)track;
}

/**
 * Read the answer to a track information question into a table of contents.
 *
 * @param block  the control block the driver filled in
 * @param track  the track asked about
 * @param toc    where the track starts, and whether it is data, are set
 * @return false if the answer is to another question or about another track
 */
inline bool dosCdParseTrackInfo(const uint8 *block, int track, DosCdToc &toc) {
	if (block[0] != kDosCdIoctlTrackInfo || block[1] != track || track < 1 || track > kDosCdMaxTrack)
		return false;
	toc.start[track] = dosCdRedBookToFrames(READ_LE_UINT32(block + 2));
	toc.data[track] = (block[6] & 0x40) != 0;
	return true;
}

/** Left and right volumes for a volume (0 to 255) and a balance (-127 to 127), as the mixer does */
inline void dosCdChannelVolumes(byte volume, int8 balance, byte &left, byte &right) {
	if (balance < -127)
		balance = -127; // an int8 can hold -128, which is past full left
	if (balance == 0) {
		left = right = volume;
	} else if (balance < 0) {
		left = volume;
		right = (byte)((127 + balance) * volume / 127);
	} else {
		right = volume;
		left = (byte)((127 - balance) * volume / 127);
	}
}

/** Channel control: left and right at the volumes given, a four channel disc's second pair silent */
inline void dosCdBuildChannels(uint8 *block, byte left, byte right) {
	block[0] = kDosCdIoctlSetChannels;
	block[1] = 0;
	block[2] = left;
	block[3] = 1;
	block[4] = right;
	block[5] = 2;
	block[6] = 0;
	block[7] = 3;
	block[8] = 0;
}

// ---- A track that plays more than once ----
// The drive plays a track once, so it is restarted when over

/** What dosCdLoopStep() found */
enum DosCdStep {
	kDosCdWait,       ///< still playing
	kDosCdReplay,     ///< over, and plays again
	kDosCdDone        ///< over, nothing more to play
};

/** The repeats of the track that plays */
struct DosCdLoop {
	int loops;               ///< plays left (-1: for ever; 0: nothing is playing)
	uint32 endTime;          ///< when the play in progress should be over, milliseconds of the system clock
	int lateExtensions;      ///< times the drive was given longer because it still said it was playing

	DosCdLoop() : loops(0), endTime(0), lateExtensions(0) {}

	/** ScummVM's numLoops: 0 and 1 play once, -1 for ever, n plays n times */
	void begin(int numLoops, uint32 now, uint32 frames) {
		loops = (numLoops == 0) ? 1 : numLoops;
		endTime = now + dosCdFramesToMillis(frames);
		lateExtensions = 0;
	}

	/** Nothing plays any more */
	void cancel() {
		loops = 0;
		lateExtensions = 0;
	}
};

/**
 * Polled with whether the drive still plays: a drive late to finish gets half a second at a
 * time, six times at most, so the end of a track is not cut off
 */
inline DosCdStep dosCdLoopStep(DosCdLoop &s, uint32 now, bool driveBusy) {
	if (s.loops == 0)
		return kDosCdDone;
	if ((int32)(now - s.endTime) < 0)
		return kDosCdWait;
	if (driveBusy && s.lateExtensions < 6) {
		s.lateExtensions++;
		s.endTime = now + 500;
		return kDosCdWait;
	}

	s.lateExtensions = 0;
	if (s.loops > 0)
		s.loops--;
	return (s.loops == 0) ? kDosCdDone : kDosCdReplay;
}

/** For isPlaying(): until the end time, and while the drive says it plays */
inline bool dosCdLoopPlaying(const DosCdLoop &s, uint32 now, bool driveBusy) {
	return s.loops != 0 && ((int32)(now - s.endTime) < 0 || driveBusy);
}

// ---- The drive in the configuration ----

/** The drive number (A is 0) of a "cdrom" setting such as "D", "d:" or "D:\"; -1 if not one */
inline int dosCdDriveLetter(const char *text) {
	if (!text)
		return -1;
	const char c = text[0];
	int letter;
	if (c >= 'a' && c <= 'z')
		letter = c - 'a';
	else if (c >= 'A' && c <= 'Z')
		letter = c - 'A';
	else
		return -1;
	if (text[1] != 0 && text[1] != ':')
		return -1;
	if (text[1] == ':' && text[2] != 0 && text[2] != '\\' && text[2] != '/')
		return -1;
	return letter;
}

// ---- The sound card's CD input ----
// The drive's music goes by cable to the sound card, which mixes its CD input in at the level its
// mixer is set to. A Sound Blaster 16's mixer has that level at 0 until a mixer program sets it.

/** The mixers whose CD input the port knows how to set */
enum DosCdMixer {
	kDosCdNoMixer,      ///< none, or one the port does not know
	kDosCdMixerSbPro,   ///< CT1345: CD volume in register 0x28, the left side in the high nibble
	kDosCdMixerSb16     ///< CT1745: CD volume in 0x36 (left) and 0x37 (right), output switches in 0x3C
};

/** Mixer registers */
enum {
	kDosCdMixerSbProVoice = 0x04,
	kDosCdMixerSbProCd = 0x28,
	kDosCdMixerSb16VoiceLeft = 0x32,
	kDosCdMixerSb16VoiceRight = 0x33,
	kDosCdMixerSb16CdLeft = 0x36,
	kDosCdMixerSb16CdRight = 0x37,
	kDosCdMixerSb16Switches = 0x3C,
	kDosCdMixerSb16SwitchesCd = 0x06    ///< CD left (bit 2) and right (bit 1) in the output
};

/**
 * The mixer of the card a BLASTER variable such as "A220 I5 D1 H5 T6" describes, and its port:
 * T6 or a 16-bit DMA channel (H) is an SB16, T2, T4 and T5 an SB Pro
 */
inline DosCdMixer dosCdBlasterMixer(const char *blaster, unsigned &base) {
	const DosBlaster card = dosParseBlaster(blaster);
	base = card.base;
	const unsigned type = card.type;
	const bool highDma = card.hdma >= 0;

	if (!base)
		return kDosCdNoMixer;
	if (type == 6 || highDma)
		return kDosCdMixerSb16;
	if (type == 2 || type == 4 || type == 5)
		return kDosCdMixerSbPro;
	return kDosCdNoMixer;
}

/**
 * SB16: the level to set a silent CD input to (both sides at 0), the voice's level, or 24 of 31
 * if that is 0 too; false if either side is above 0, which is left as the user set it
 */
inline bool dosCdSb16CdLevel(uint8 cdLeft, uint8 cdRight, uint8 voice, uint8 &level) {
	if ((cdLeft >> 3) != 0 || (cdRight >> 3) != 0)
		return false;
	level = (voice >> 3) != 0 ? (uint8)(voice & 0xF8) : (uint8)(24 << 3);
	return true;
}

/** SB16 output switches with the CD let through, unless one of its sides already is */
inline uint8 dosCdSb16Switches(uint8 switches) {
	if (switches & kDosCdMixerSb16SwitchesCd)
		return switches;
	return (uint8)(switches | kDosCdMixerSb16SwitchesCd);
}

/** SB Pro: the same for its CD volume (3 bits a side, in bits 7-5 and 3-1), from the voice's */
inline bool dosCdSbProCdLevel(uint8 cd, uint8 voice, uint8 &level) {
	if (((cd >> 5) & 7) != 0 || ((cd >> 1) & 7) != 0)
		return false;
	level = (((voice >> 5) & 7) != 0 || ((voice >> 1) & 7) != 0) ? voice : (uint8)0xCC;
	return true;
}

#endif
