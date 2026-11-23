#include <cxxtest/TestSuite.h>

#include <string.h>

#include "backends/platform/dos/dos-modes.h"

/**
 * Choosing the resolution of the launcher on the DOS port: reading a size, what a monitor's EDID block
 * says it can show, and picking the largest video card mode that suits. See dos-modes.h.
 */
class DosModesTestSuite : public CxxTest::TestSuite {
	/** A 128 byte EDID block with the header and nothing else */
	static void blankEdid(uint8_t *e) {
		memset(e, 0, 128);
		static const uint8_t header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
		memcpy(e, header, 8);
	}

	/** ... with a first detailed timing of w x h */
	static void setDetailedTiming(uint8_t *e, int w, int h) {
		e[54] = 0x01;                                          // a pixel clock, so the slot is a timing
		e[56] = w & 0xFF;
		e[58] = (uint8_t)((w >> 8) << 4);
		e[59] = h & 0xFF;
		e[61] = (uint8_t)((h >> 8) << 4);
	}

public:
	void test_parse_size() {
		int w = 0, h = 0;
		TS_ASSERT(dosParseSize("800x600", w, h));
		TS_ASSERT_EQUALS(w, 800);
		TS_ASSERT_EQUALS(h, 600);

		TS_ASSERT(dosParseSize("1024X768", w, h));               // either case of the x
		TS_ASSERT_EQUALS(w, 1024);
		TS_ASSERT_EQUALS(h, 768);

		TS_ASSERT(dosParseSize("320x200", w, h));
		TS_ASSERT_EQUALS(w, 320);
		TS_ASSERT_EQUALS(h, 200);
	}

	void test_parse_size_refusals() {
		int w = 7, h = 7;
		TS_ASSERT(!dosParseSize(nullptr, w, h));
		TS_ASSERT(!dosParseSize("", w, h));
		TS_ASSERT(!dosParseSize("800", w, h));
		TS_ASSERT(!dosParseSize("800x", w, h));
		TS_ASSERT(!dosParseSize("x600", w, h));
		TS_ASSERT(!dosParseSize("0x600", w, h));                 // no width
		TS_ASSERT(!dosParseSize("800x0", w, h));
		TS_ASSERT(!dosParseSize("-800x600", w, h));
		TS_ASSERT(!dosParseSize("9000x600", w, h));              // larger than any screen
		TS_ASSERT(!dosParseSize("800x9000", w, h));
		TS_ASSERT(!dosParseSize("abc", w, h));
		TS_ASSERT_EQUALS(w, 7);                                  // untouched when refused
		TS_ASSERT_EQUALS(h, 7);
	}

	void test_edid_native_resolution_of_a_flat_panel() {
		uint8_t e[128];
		blankEdid(e);
		setDetailedTiming(e, 1024, 768);
		int w = 0, h = 0;
		TS_ASSERT(dosEdidSize(e, w, h));
		TS_ASSERT_EQUALS(w, 1024);
		TS_ASSERT_EQUALS(h, 768);

		setDetailedTiming(e, 1920, 1080);                        // needs the high bits of both numbers
		TS_ASSERT(dosEdidSize(e, w, h));
		TS_ASSERT_EQUALS(w, 1920);
		TS_ASSERT_EQUALS(h, 1080);
	}

	void test_edid_detailed_timing_wins_over_the_established_ones() {
		uint8_t e[128];
		blankEdid(e);
		e[36] |= 0x01;                                           // established: 1280x1024
		setDetailedTiming(e, 800, 600);
		int w = 0, h = 0;
		TS_ASSERT(dosEdidSize(e, w, h));
		TS_ASSERT_EQUALS(w, 800);
		TS_ASSERT_EQUALS(h, 600);
	}

	void test_edid_without_detailed_timing_takes_the_largest_established() {
		uint8_t e[128];
		blankEdid(e);
		e[35] |= 0x20;                                           // 640x480 at 60 Hz
		e[36] |= 0x08;                                           // 1024x768
		int w = 0, h = 0;
		TS_ASSERT(dosEdidSize(e, w, h));
		TS_ASSERT_EQUALS(w, 1024);
		TS_ASSERT_EQUALS(h, 768);
	}

	void test_edid_standard_timings() {
		uint8_t e[128];
		blankEdid(e);
		for (int i = 0; i < 8; i++)
			e[38 + i * 2] = e[39 + i * 2] = 0x01;                // all unused
		e[38] = (1280 / 8) - 31;                                 // 1280 wide...
		e[39] = 1 << 6;                                          // ...at 4:3
		int w = 0, h = 0;
		TS_ASSERT(dosEdidSize(e, w, h));
		TS_ASSERT_EQUALS(w, 1280);
		TS_ASSERT_EQUALS(h, 960);

		e[39] = 3 << 6;                                          // the same width at 16:9
		TS_ASSERT(dosEdidSize(e, w, h));
		TS_ASSERT_EQUALS(w, 1280);
		TS_ASSERT_EQUALS(h, 720);
	}

	void test_edid_refusals() {
		uint8_t e[128];
		int w = 0, h = 0;

		memset(e, 0, sizeof(e));                                 // no header
		TS_ASSERT(!dosEdidSize(e, w, h));

		blankEdid(e);                                            // a header and no timings at all
		TS_ASSERT(!dosEdidSize(e, w, h));

		blankEdid(e);
		setDetailedTiming(e, 100, 100);                          // a timing too small to be a screen
		TS_ASSERT(!dosEdidSize(e, w, h));
	}

	void test_aspect_ratios() {
		TS_ASSERT(dosAspectMatches(800, 600, 4, 3));
		TS_ASSERT(dosAspectMatches(1024, 768, 4, 3));
		TS_ASSERT(dosAspectMatches(640, 480, 4, 3));
		TS_ASSERT(dosAspectMatches(1366, 768, 16, 9));           // the near miss of 1366 wide panels is accepted
		TS_ASSERT(!dosAspectMatches(1280, 1024, 4, 3));          // 5:4
		TS_ASSERT(!dosAspectMatches(640, 400, 4, 3));            // 16:10
		TS_ASSERT(dosAspectMatches(1280, 800, 16, 10));
	}

	void test_pick_the_largest_mode_within_the_limit() {
		const DosModeSize modes[] = { {320, 200}, {640, 400}, {640, 480}, {800, 600}, {1024, 768}, {1280, 1024} };
		DosModeSize out = { 0, 0 };
		TS_ASSERT(dosPickLargest(modes, 6, 1024, 768, 4, 3, out));
		TS_ASSERT_EQUALS(out.w, 1024);
		TS_ASSERT_EQUALS(out.h, 768);

		TS_ASSERT(dosPickLargest(modes, 6, 900, 700, 4, 3, out));
		TS_ASSERT_EQUALS(out.w, 800);
		TS_ASSERT_EQUALS(out.h, 600);

		TS_ASSERT(dosPickLargest(modes, 6, 4000, 4000, 4, 3, out));    // the 5:4 mode is not 4:3
		TS_ASSERT_EQUALS(out.w, 1024);

		TS_ASSERT(!dosPickLargest(modes, 6, 639, 479, 4, 3, out));      // nothing small enough that is a VESA mode
	}

	void test_the_320x200_mode_is_never_a_launcher_mode() {
		const DosModeSize modes[] = { {320, 200} };
		DosModeSize out = { 0, 0 };
		TS_ASSERT(!dosPickLargest(modes, 1, 1024, 768, 8, 5, out));
		TS_ASSERT(!dosChooseGuiMode(modes, 1, 1024, 768, false, 0, 0, out));
	}

	void test_choose_without_a_monitor_answer() {
		const DosModeSize modes[] = { {640, 480}, {800, 600}, {1024, 768}, {1280, 800} };
		DosModeSize out = { 0, 0 };
		TS_ASSERT(dosChooseGuiMode(modes, 4, 1024, 768, false, 0, 0, out));
		TS_ASSERT_EQUALS(out.w, 1024);
		TS_ASSERT_EQUALS(out.h, 768);

		TS_ASSERT(dosChooseGuiMode(modes, 4, 800, 600, false, 0, 0, out));
		TS_ASSERT_EQUALS(out.w, 800);
	}

	void test_choose_a_mode_of_the_monitors_own_shape() {
		const DosModeSize modes[] = { {640, 480}, {800, 600}, {1024, 768}, {1280, 800} };
		DosModeSize out = { 0, 0 };
		TS_ASSERT(dosChooseGuiMode(modes, 4, 1280, 1024, true, 1280, 800, out));   // a 16:10 panel gets its own mode
		TS_ASSERT_EQUALS(out.w, 1280);
		TS_ASSERT_EQUALS(out.h, 800);
	}

	void test_choose_falls_back_to_4_3_within_the_monitors_size() {
		const DosModeSize modes[] = { {640, 480}, {800, 600}, {1024, 768}, {1280, 1024} };
		DosModeSize out = { 0, 0 };
		// A 1280x720 panel: no mode of its shape, and 1024x768 is taller than it is
		TS_ASSERT(dosChooseGuiMode(modes, 4, 1280, 1024, true, 1280, 720, out));
		TS_ASSERT_EQUALS(out.w, 800);
		TS_ASSERT_EQUALS(out.h, 600);
	}

	void test_choose_nothing() {
		DosModeSize out = { 0, 0 };
		TS_ASSERT(!dosChooseGuiMode(nullptr, 0, 1024, 768, false, 0, 0, out));
		const DosModeSize big[] = { {1280, 1024} };
		TS_ASSERT(!dosChooseGuiMode(big, 1, 1024, 768, false, 0, 0, out));
	}
};
