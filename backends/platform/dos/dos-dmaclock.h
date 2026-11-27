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

#ifndef BACKENDS_PLATFORM_DOS_DMACLOCK_H
#define BACKENDS_PLATFORM_DOS_DMACLOCK_H

#include "common/scummsys.h"

// Where a Sound Blaster's looping DMA transfer has got to, for dos-sb.cpp and dos-mixer.cpp: the
// DMA controller's count turned into a place in the ring, that place followed round the ring with
// the help of a clock, and the clock the mixer runs on when there is no card. No DOS calls, so
// unit tested on the host.

/**
 * Turn a DMA channel's count register into the frame of the ring the card is playing.
 *
 * @param count       the count register: units left, minus one (it wraps to 0xFFFF at terminal count)
 * @param wordUnits   true on the 16-bit channels (5 to 7), which count words instead of bytes
 * @param ringBytes   the size of the ring in bytes
 * @param frameBytes  the size of one frame in bytes
 * @return the frame, or 0 if the count is more than the ring holds
 */
inline uint dosDmaCountToFrame(uint count, bool wordUnits, uint ringBytes, uint frameBytes) {
	const uint remaining = (count + 1) * (wordUnits ? 2 : 1);
	if (remaining > ringBytes)
		return 0;
	return (ringBytes - remaining) / frameBytes;
}

/**
 * How far a position moved forward in a ring, taking the wrap from the end to the start into account.
 *
 * @param from  the earlier position
 * @param to    the later position
 * @param size  the size of the ring
 * @return the frames from `from` to `to`, less than one lap of the ring
 */
inline uint dosDmaRingDistance(uint from, uint to, uint size) {
	return (to + size - from) % size;
}

/**
 * The card's position in frames since it started, followed from its place in the ring at each poll.
 * The ring wraps several times a second, so the clock says how many whole laps went by unseen
 * while the program was busy.
 */
struct DosDmaClock {
	uint ringFrames;        ///< size of the ring, in frames
	uint rate;              ///< frames the card plays in a second
	uint64 ticksPerSecond;  ///< ticks of the clock given to dosDmaClockPoll() (UCLOCKS_PER_SEC for uclock())
	uint lastPos;           ///< place in the ring at the previous poll
	uint64 lastTick;        ///< clock at the previous poll
	uint64 played;          ///< frames the card has played since dosDmaClockStart()
	uint64 stuckSince;      ///< clock when the place in the ring was last seen moving
};

/**
 * Start following a card.
 *
 * @param clock           the state to set up
 * @param ringFrames      the size of the ring, in frames
 * @param rate            frames the card plays in a second
 * @param ticksPerSecond  ticks of the clock in a second
 * @param pos             where the card is in the ring now
 * @param now             the clock now
 */
inline void dosDmaClockStart(DosDmaClock &clock, uint ringFrames, uint rate, uint64 ticksPerSecond, uint pos, uint64 now) {
	clock.ringFrames = ringFrames;
	clock.rate = rate;
	clock.ticksPerSecond = ticksPerSecond;
	clock.lastPos = pos;
	clock.lastTick = now;
	clock.played = 0;
	clock.stuckSince = now;
}

/**
 * Follow the card to its new place in the ring, adding what it played since the previous poll to
 * DosDmaClock::played. A clock that steps back counts as no time.
 *
 * The card counts as stopped when its place has not moved for 0.4 seconds of polls that came
 * at most 0.1 seconds apart (longer gaps say nothing: the ring may have gone round exactly).
 *
 * @param clock  the state, from dosDmaClockStart()
 * @param pos    where the card is in the ring now
 * @param now    the clock now
 * @return false if the card stopped
 */
inline bool dosDmaClockPoll(DosDmaClock &clock, uint pos, uint64 now) {
	if (now < clock.lastTick)
		now = clock.lastTick;
	const uint64 sinceLast = now - clock.lastTick;
	const uint size = clock.ringFrames;

	// What the ring shows, plus the whole laps the clock says went by unseen
	const uint moved = dosDmaRingDistance(clock.lastPos, pos, size);
	const uint64 expected = sinceLast * clock.rate / clock.ticksPerSecond;
	const uint64 laps = expected > moved ? (expected - moved + size / 2) / size : 0;
	clock.played += moved + laps * size;

	if (moved != 0 || sinceLast > clock.ticksPerSecond / 10)
		clock.stuckSince = now;
	else if (now - clock.stuckSince > clock.ticksPerSecond * 4 / 10)
		return false;

	clock.lastPos = pos;
	clock.lastTick = now;
	return true;
}

/**
 * For the mixer without a card: how many frames real audio would have played since `lastTick`.
 * `lastTick` moves on by the time those frames take (rounded down, never past `now`), so the remainder
 * counts towards the next call.
 * After a long stall at most a quarter of a second is caught up, and the rest is dropped.
 *
 * @param lastTick        the clock when the mixer last ran; updated
 * @param now             the clock now (a clock that steps back gives no frames)
 * @param rate            frames in a second
 * @param ticksPerSecond  ticks of the clock in a second
 * @return the frames to mix now
 */
inline uint64 dosNullFramesDue(uint64 &lastTick, uint64 now, uint rate, uint64 ticksPerSecond) {
	if (now <= lastTick)
		return 0;
	uint64 due = (now - lastTick) * rate / ticksPerSecond;
	if (!due)
		return 0;

	const uint64 maxCatchUp = rate / 4;
	if (due > maxCatchUp) {
		due = maxCatchUp;
		lastTick = now;
	} else {
		lastTick += due * ticksPerSecond / rate;
	}
	return due;
}

#endif
