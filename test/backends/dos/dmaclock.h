#include <cxxtest/TestSuite.h>

#include "backends/platform/dos/dos-dmaclock.h"

/**
 * Where the Sound Blaster's looping DMA transfer has got to: the DMA controller's count turned into a
 * frame of the ring, that frame followed round the ring with a clock counting the laps nobody saw, the
 * check that the card has stopped, and the clock the mixer runs on without a card. See dos-dmaclock.h.
 */
class DosDmaClockTestSuite : public CxxTest::TestSuite {
	// A small, easily checked card: 8 frames a millisecond on a 1 ms clock, so a lap of the ring takes 512 ms
	enum {
		kRing = 4096,
		kRate = 8000,
		kTicks = 1000,
		kUclocksPerSec = 1193180   ///< the real uclock() rate
	};

	DosDmaClock startedAt(uint pos, uint64 now) {
		DosDmaClock clock;
		dosDmaClockStart(clock, kRing, kRate, kTicks, pos, now);
		return clock;
	}

public:
	void test_a_full_count_is_the_start_of_the_ring() {
		// 8-bit mono: a byte a frame, on an 8-bit channel
		TS_ASSERT_EQUALS(dosDmaCountToFrame(8191, false, 8192, 1), 0u);     // all 8192 bytes still to go
		TS_ASSERT_EQUALS(dosDmaCountToFrame(4095, false, 8192, 1), 4096u);  // half way
		TS_ASSERT_EQUALS(dosDmaCountToFrame(0, false, 8192, 1), 8191u);     // the last byte
	}

	void test_the_16_bit_channels_count_words() {
		// 16-bit stereo on channel 5: 4 bytes a frame, and the count is in words
		TS_ASSERT_EQUALS(dosDmaCountToFrame(16383, true, 32768, 4), 0u);
		TS_ASSERT_EQUALS(dosDmaCountToFrame(8191, true, 32768, 4), 4096u);
		TS_ASSERT_EQUALS(dosDmaCountToFrame(0, true, 32768, 4), 8191u);     // 2 bytes left: inside the last frame
	}

	void test_a_frame_of_several_bytes_on_an_8_bit_channel() {
		TS_ASSERT_EQUALS(dosDmaCountToFrame(8191, false, 8192, 2), 0u);
		TS_ASSERT_EQUALS(dosDmaCountToFrame(4095, false, 8192, 2), 2048u);
		TS_ASSERT_EQUALS(dosDmaCountToFrame(4096, false, 8192, 2), 2047u);  // part of a frame left: still that frame
	}

	void test_a_count_past_the_ring_is_the_start() {
		TS_ASSERT_EQUALS(dosDmaCountToFrame(0xFFFF, false, 8192, 1), 0u);   // terminal count wraps to 0xFFFF
		TS_ASSERT_EQUALS(dosDmaCountToFrame(0xFFFF, true, 32768, 4), 0u);
		TS_ASSERT_EQUALS(dosDmaCountToFrame(8192, false, 8192, 1), 0u);     // one more than the ring holds
	}

	void test_distance_round_the_ring() {
		TS_ASSERT_EQUALS(dosDmaRingDistance(100, 300, kRing), 200u);
		TS_ASSERT_EQUALS(dosDmaRingDistance(300, 300, kRing), 0u);
		TS_ASSERT_EQUALS(dosDmaRingDistance(4000, 100, kRing), 196u);       // over the end and back to the start
		TS_ASSERT_EQUALS(dosDmaRingDistance(0, kRing - 1, kRing), (uint)kRing - 1);
	}

	void test_a_position_just_behind_is_almost_a_lap() {
		// The ring only ever moves forward: a step back reads as nearly a whole lap
		TS_ASSERT_EQUALS(dosDmaRingDistance(300, 299, kRing), (uint)kRing - 1);
	}

	void test_starting() {
		const DosDmaClock clock = startedAt(1234, 5000);
		TS_ASSERT_EQUALS(clock.ringFrames, (uint)kRing);
		TS_ASSERT_EQUALS(clock.rate, (uint)kRate);
		TS_ASSERT_EQUALS(clock.ticksPerSecond, (uint64)kTicks);
		TS_ASSERT_EQUALS(clock.lastPos, 1234u);
		TS_ASSERT_EQUALS(clock.lastTick, 5000u);
		TS_ASSERT_EQUALS(clock.played, 0u);
		TS_ASSERT_EQUALS(clock.stuckSince, 5000u);
	}

	void test_polls_add_up_what_was_played() {
		DosDmaClock clock = startedAt(0, 0);
		TS_ASSERT(dosDmaClockPoll(clock, 80, 10));
		TS_ASSERT_EQUALS(clock.played, 80u);
		TS_ASSERT(dosDmaClockPoll(clock, 240, 30));
		TS_ASSERT_EQUALS(clock.played, 240u);
		TS_ASSERT_EQUALS(clock.lastPos, 240u);
		TS_ASSERT_EQUALS(clock.lastTick, 30u);
	}

	void test_the_wrap_at_the_end_of_the_ring() {
		DosDmaClock clock = startedAt(4000, 0);
		TS_ASSERT(dosDmaClockPoll(clock, 100, 24));                      // 192 frames by the clock, 196 by the ring
		TS_ASSERT_EQUALS(clock.played, 196u);                            // the ring is exact; the clock only counts laps
	}

	void test_a_lap_nobody_saw_is_counted() {
		DosDmaClock clock = startedAt(0, 0);
		TS_ASSERT(dosDmaClockPoll(clock, 704, 600));                     // 600 ms is 4800 frames: one lap and 704
		TS_ASSERT_EQUALS(clock.played, 4800u);
	}

	void test_several_laps_nobody_saw() {
		DosDmaClock clock = startedAt(0, 0);
		TS_ASSERT(dosDmaClockPoll(clock, 416, 2100));                    // 16800 frames: four laps and 416
		TS_ASSERT_EQUALS(clock.played, 16800u);
	}

	void test_exactly_one_lap_between_polls() {
		// The card is back where it was, but the clock says a whole lap went by: neither nothing nor stuck
		DosDmaClock clock = startedAt(500, 0);
		for (int i = 1; i <= 5; i++) {
			TS_ASSERT(dosDmaClockPoll(clock, 500, i * 512));
			TS_ASSERT_EQUALS(clock.played, (uint64)i * kRing);
		}
	}

	void test_the_clock_picks_the_nearest_lap() {
		// The ring says 100 frames; the clock is out by up to half a lap either way
		DosDmaClock clock = startedAt(0, 0);
		TS_ASSERT(dosDmaClockPoll(clock, 100, 5));                       // the clock says 40: a little slow
		TS_ASSERT_EQUALS(clock.played, 100u);

		clock = startedAt(0, 0);
		TS_ASSERT(dosDmaClockPoll(clock, 100, 268));                     // 2144 frames: 2044 more than the ring, under half a lap
		TS_ASSERT_EQUALS(clock.played, 100u);

		clock = startedAt(0, 0);
		TS_ASSERT(dosDmaClockPoll(clock, 100, 269));                     // 2152 frames: 2052 more, over half a lap
		TS_ASSERT_EQUALS(clock.played, 100u + kRing);
	}

	void test_a_clock_that_steps_back_counts_as_no_time() {
		DosDmaClock clock = startedAt(0, 1000);
		TS_ASSERT(dosDmaClockPoll(clock, 50, 990));
		TS_ASSERT_EQUALS(clock.played, 50u);                             // what the ring shows still counts
		TS_ASSERT_EQUALS(clock.lastTick, 1000u);                         // time never runs backwards
		TS_ASSERT(dosDmaClockPoll(clock, 130, 1010));
		TS_ASSERT_EQUALS(clock.played, 130u);
	}

	void test_a_card_that_stops_is_noticed() {
		DosDmaClock clock = startedAt(700, 0);
		for (uint64 now = 50; now <= 400; now += 50)
			TS_ASSERT(dosDmaClockPoll(clock, 700, now));                 // not moved for 0.4 seconds: not yet
		TS_ASSERT(!dosDmaClockPoll(clock, 700, 450));                    // more than 0.4 seconds
		TS_ASSERT_EQUALS(clock.played, 0u);
	}

	void test_a_card_that_moves_is_never_stopped() {
		DosDmaClock clock = startedAt(0, 0);
		uint pos = 0;
		for (uint64 now = 50; now <= 5000; now += 50) {
			pos = (pos + 400) % kRing;
			TS_ASSERT(dosDmaClockPoll(clock, pos, now));
		}
		TS_ASSERT_EQUALS(clock.played, 40000u);
	}

	void test_moving_again_starts_the_wait_again() {
		DosDmaClock clock = startedAt(0, 0);
		for (uint64 now = 50; now <= 350; now += 50)
			TS_ASSERT(dosDmaClockPoll(clock, 0, now));
		TS_ASSERT(dosDmaClockPoll(clock, 16, 400));                      // it moved
		for (uint64 now = 450; now <= 800; now += 50)
			TS_ASSERT(dosDmaClockPoll(clock, 16, now));
		TS_ASSERT(!dosDmaClockPoll(clock, 16, 850));
	}

	void test_polls_far_apart_say_nothing_about_stopping() {
		// Over 0.1 seconds between polls: a card in the same place may have gone round, so it is not called stopped
		DosDmaClock clock = startedAt(700, 0);
		for (uint64 now = 200; now <= 2000; now += 200)
			TS_ASSERT(dosDmaClockPoll(clock, 700, now));
		TS_ASSERT_EQUALS(clock.played, 0u);                              // 1600 frames by the clock is under half a lap
	}

	void test_following_a_real_card_for_a_minute() {
		// A 22050 Hz card on uclock(), polled at uneven times from 1 ms to over a second apart (a busy
		// game): the total is exact, as the clock is never out by half a lap
		DosDmaClock clock;
		const uint64 start = (uint64)1 << 40;                          // a clock that has run for a long time
		dosDmaClockStart(clock, kRing, 22050, kUclocksPerSec, 0, start);
		uint32 seed = 12345;
		uint64 elapsed = 0;
		while (elapsed < 60 * kUclocksPerSec) {
			seed = seed * 1103515245 + 12345;
			elapsed += 1193 + (seed >> 8) % (kUclocksPerSec + kUclocksPerSec / 4);
			const uint64 frames = elapsed * 22050 / kUclocksPerSec;
			TS_ASSERT(dosDmaClockPoll(clock, (uint)(frames % kRing), start + elapsed));
			TS_ASSERT_EQUALS(clock.played, frames);
		}
	}

	void test_without_a_card_frames_come_with_time() {
		uint64 lastTick = 1000;
		TS_ASSERT_EQUALS(dosNullFramesDue(lastTick, 1000 + kUclocksPerSec / 100, 22050, kUclocksPerSec), 220u);   // 10 ms
		TS_ASSERT_LESS_THAN(1000u, lastTick);
		TS_ASSERT_LESS_THAN_EQUALS(lastTick, 1000u + kUclocksPerSec / 100);              // never past now
	}

	void test_without_a_card_too_little_time_waits() {
		uint64 lastTick = 1000;
		TS_ASSERT_EQUALS(dosNullFramesDue(lastTick, 1040, 22050, kUclocksPerSec), 0u);     // under one frame
		TS_ASSERT_EQUALS(lastTick, 1000u);                                                  // kept for the next call
		TS_ASSERT_EQUALS(dosNullFramesDue(lastTick, 1060, 22050, kUclocksPerSec), 1u);     // now it adds up to one
	}

	void test_without_a_card_a_second_of_small_steps_is_a_second_of_sound() {
		uint64 lastTick = 0;
		uint64 total = 0;
		for (uint64 now = 1193; now <= kUclocksPerSec; now += 1193)                         // about every millisecond
			total += dosNullFramesDue(lastTick, now, 22050, kUclocksPerSec);
		TS_ASSERT_LESS_THAN_EQUALS(22050u - 22, total);                                    // within 0.1% of real time
		TS_ASSERT_LESS_THAN_EQUALS(total, 22050u + 22);
	}

	void test_without_a_card_a_stall_is_not_caught_up() {
		uint64 lastTick = 1000;
		TS_ASSERT_EQUALS(dosNullFramesDue(lastTick, 1000 + 2 * kUclocksPerSec, 22050, kUclocksPerSec), 22050u / 4);
		TS_ASSERT_EQUALS(lastTick, 1000 + 2 * kUclocksPerSec);                              // the rest is dropped
		TS_ASSERT_EQUALS(dosNullFramesDue(lastTick, lastTick + kUclocksPerSec / 100, 22050, kUclocksPerSec), 220u);
	}

	void test_without_a_card_a_clock_that_steps_back_gives_nothing() {
		uint64 lastTick = 5000;
		TS_ASSERT_EQUALS(dosNullFramesDue(lastTick, 4000, 22050, kUclocksPerSec), 0u);
		TS_ASSERT_EQUALS(lastTick, 5000u);
		TS_ASSERT_EQUALS(dosNullFramesDue(lastTick, 5000, 22050, kUclocksPerSec), 0u);
	}
};
