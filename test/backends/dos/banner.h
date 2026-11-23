#include <cxxtest/TestSuite.h>

#include <string.h>

#include "backends/platform/dos/dos-banner.h"

/**
 * The banner shown while a game starts on the DOS port: its size on screens of different resolutions, and how its
 * text is broken into lines. See dos-banner.h.
 */
class DosBannerTestSuite : public CxxTest::TestSuite {
public:
	void test_layout_vga() {
		// VGA mode 13h: the font doubled, as the own backend has always drawn it
		const DosBannerLayout l = dosBannerLayout(320, 200);
		TS_ASSERT_EQUALS(l.scale, 2);
		TS_ASSERT_EQUALS(l.margin, 8);
		TS_ASSERT_EQUALS(l.glyph, 16);
		TS_ASSERT_EQUALS(l.lineStep, 18);
		TS_ASSERT_EQUALS(l.perLine, 19);
		TS_ASSERT_EQUALS(l.maxLines, 10);
	}

	void test_layout_scales_with_screen() {
		// The same share of the screen at a higher resolution
		TS_ASSERT_EQUALS(dosBannerLayout(640, 400).scale, 4);
		TS_ASSERT_EQUALS(dosBannerLayout(640, 400).perLine, 19);
		TS_ASSERT_EQUALS(dosBannerLayout(640, 480).scale, 4);
		TS_ASSERT_EQUALS(dosBannerLayout(800, 600).scale, 4);
		TS_ASSERT_EQUALS(dosBannerLayout(1024, 768).scale, 6);
		TS_ASSERT_EQUALS(dosBannerLayout(1280, 1024).scale, 8);
	}

	void test_layout_small_screen() {
		const DosBannerLayout l = dosBannerLayout(100, 60);
		TS_ASSERT_EQUALS(l.scale, 2);
		TS_ASSERT_EQUALS(l.perLine, 8);
		TS_ASSERT(l.maxLines >= 1);
	}

	void test_layout_fits_screen() {
		static const int sizes[][2] = { { 320, 200 }, { 320, 240 }, { 640, 400 }, { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 }, { 1600, 1200 } };
		for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
			const DosBannerLayout l = dosBannerLayout(sizes[i][0], sizes[i][1]);
			TS_ASSERT(l.margin + l.perLine * l.glyph <= sizes[i][0]);
			TS_ASSERT(l.margin + l.maxLines * l.lineStep <= sizes[i][1]);
		}
	}

	void test_wrap_short() {
		char out[64];
		TS_ASSERT_EQUALS(dosBannerWrap("Hello\n", 20, 3, out, sizeof(out)), 1);
		TS_ASSERT_EQUALS(strcmp(out, "Hello\n"), 0);
	}

	void test_wrap_at_spaces() {
		char out[64];
		TS_ASSERT_EQUALS(dosBannerWrap("one two three four", 9, 5, out, sizeof(out)), 3);
		TS_ASSERT_EQUALS(strcmp(out, "one two\nthree\nfour\n"), 0);
		TS_ASSERT_EQUALS(dosBannerWrap("one two three four", 10, 5, out, sizeof(out)), 2);
		TS_ASSERT_EQUALS(strcmp(out, "one two\nthree four\n"), 0);
	}

	void test_wrap_exact_width() {
		char out[64];
		TS_ASSERT_EQUALS(dosBannerWrap("abcd efgh", 4, 5, out, sizeof(out)), 2);
		TS_ASSERT_EQUALS(strcmp(out, "abcd\nefgh\n"), 0);
	}

	void test_wrap_long_word() {
		char out[64];
		TS_ASSERT_EQUALS(dosBannerWrap("abcdefghij", 4, 5, out, sizeof(out)), 3);
		TS_ASSERT_EQUALS(strcmp(out, "abcd\nefgh\nij\n"), 0);
	}

	void test_wrap_newlines() {
		char out[64];
		TS_ASSERT_EQUALS(dosBannerWrap("ab\n\ncd\n", 10, 5, out, sizeof(out)), 2);
		TS_ASSERT_EQUALS(strcmp(out, "ab\ncd\n"), 0);
	}

	void test_wrap_max_lines() {
		char out[64];
		TS_ASSERT_EQUALS(dosBannerWrap("aa bb cc dd", 2, 2, out, sizeof(out)), 2);
		TS_ASSERT_EQUALS(strcmp(out, "aa\nbb\n"), 0);
	}

	void test_wrap_launch_message() {
		// The message the launcher logs as a game starts, on the 320x200 screen: all of it, broken between words
		const char *msg = "User picked target 'testbed' (engine ID 'testbed', game ID 'testbed')...\n";
		const DosBannerLayout l = dosBannerLayout(320, 200);
		char out[256];
		const int lines = dosBannerWrap(msg, l.perLine, l.maxLines, out, sizeof(out));
		TS_ASSERT_EQUALS(strcmp(out, "User picked target\n'testbed' (engine\nID 'testbed', game\nID 'testbed')...\n"), 0);
		TS_ASSERT_EQUALS(lines, 4);
	}

	void test_wrap_small_buffer() {
		// Room for "abc\ndef\n" and its terminator only
		char out[9];
		TS_ASSERT_EQUALS(dosBannerWrap("abc def ghi", 3, 5, out, sizeof(out)), 2);
		TS_ASSERT_EQUALS(strcmp(out, "abc\ndef\n"), 0);
	}
};
