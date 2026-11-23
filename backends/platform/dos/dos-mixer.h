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

#ifndef BACKENDS_PLATFORM_DOS_MIXER_H
#define BACKENDS_PLATFORM_DOS_MIXER_H

#include "backends/mixer/mixer.h"
#include "backends/platform/dos/dos-sb.h"

/**
 * ScummVM's mixer, played through a Sound Blaster (see dos-sb.h). update(),
 * called from pollEvent() and delayMillis(), keeps the DMA ring topped up, as
 * there is no interrupt to do it. Without a card the mixer runs on the clock, silently.
 */
class DOSMixerManager : public MixerManager {
public:
	DOSMixerManager();
	~DOSMixerManager() override;

	void init() override;

	/** Feed the sound card. Cheap when there is nothing to do. */
	void update();

	void suspendAudio() override;
	int resumeAudio() override;

	bool isNullDevice() const override {
		return !_hardware;
	}

private:
	enum {
		kChunkFrames = 512,    ///< Mixed in pieces of at most this many frames
		kTargetAhead = 4096,   ///< Keep this many frames buffered (~185 ms)
		kResyncMargin = 1024,  ///< After falling behind, restart this far ahead of the card (~46 ms)
		kMinRefill = 512       ///< Do not refill for less than this (see updateHardware())
	};

	void updateHardware();
	void updateNull();

	/** Mix `frames` frames and convert them to unsigned 8-bit mono. */
	void produce(unsigned frames, uint8 *out);

	/** Mix `frames` frames of 16-bit stereo into _mixBuf, as the SB16 plays them. */
	void produce16(unsigned frames);

	/** The card stopped playing: carry on silently, on the clock. */
	void fallBackToNull(const char *why);

	SoundBlaster _sb;
	bool _hardware;
	unsigned _rate;

	unsigned _writePos;        ///< Next ring offset to write
	uint64 _lastTick;          ///< For the null device: when we last ran

	// The card's position in frames since the start (the clock counts the ring's laps)

	unsigned _lastPos;         ///< Ring position at the previous call
	uint64 _lastPollTick;      ///< uclock() at the previous call
	uint64 _playAbs;           ///< Frames the card has played
	uint64 _writtenAbs;        ///< Frames we have given it (always ahead of _playAbs when all is well)
	unsigned _underruns;       ///< Times the card ran past everything we had written

	// Watchdog: has the DMA position stopped moving?

	uint64 _stuckSince;

	int16 _mixBuf[kChunkFrames * 2];
	uint8 _outBuf[kChunkFrames];
};

#endif
