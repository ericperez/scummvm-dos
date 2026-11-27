#include <cxxtest/TestSuite.h>

#include <math.h>
#include <string.h>

#include "backends/platform/dos/dos-dither.h"

/**
 * Showing a high colour game on a 256 colour screen: the colour cube palette, the ordered dithering
 * tables and the line converter of the DOS port. See dos-dither.h.
 */
class DosDitherTestSuite : public CxxTest::TestSuite {
	/** A small deterministic stream of numbers */
	static unsigned nextRandom(unsigned &state) {
		state = state * 1664525u + 1013904223u;
		return state >> 8;
	}

	/** The three dither tables of a pixel format with the given channel widths */
	struct Tables {
		uint8_t r[16 * 256], g[16 * 256], b[16 * 256];

		Tables(int rBits, int gBits, int bBits) {
			dosDitherChannelTable(r, rBits, kDitherRed, kDitherMulRed);
			dosDitherChannelTable(g, gBits, kDitherGreen, kDitherMulGreen);
			dosDitherChannelTable(b, bBits, kDitherBlue, kDitherMulBlue);
		}
	};

public:
	void test_the_cube_fills_the_palette() {
		TS_ASSERT_EQUALS((int)kDitherColours, 252);
		TS_ASSERT_EQUALS((int)kDitherMulRed, 42);
		TS_ASSERT_EQUALS((int)kDitherMulGreen, 6);
		TS_ASSERT_EQUALS((int)kDitherMulBlue, 1);
	}

	void test_palette_corners() {
		uint8_t pal[256][3];
		dosDitherPalette(pal);

		TS_ASSERT_EQUALS(pal[0][0], 0);                          // black
		TS_ASSERT_EQUALS(pal[0][1], 0);
		TS_ASSERT_EQUALS(pal[0][2], 0);
		TS_ASSERT_EQUALS(pal[251][0], 255);                      // white: the last of the cube
		TS_ASSERT_EQUALS(pal[251][1], 255);
		TS_ASSERT_EQUALS(pal[251][2], 255);

		const int pure = 5 * kDitherMulRed;                      // full red, no green, no blue
		TS_ASSERT_EQUALS(pal[pure][0], 255);
		TS_ASSERT_EQUALS(pal[pure][1], 0);
		TS_ASSERT_EQUALS(pal[pure][2], 0);

		const int green = 6 * kDitherMulGreen;
		TS_ASSERT_EQUALS(pal[green][0], 0);
		TS_ASSERT_EQUALS(pal[green][1], 255);
		TS_ASSERT_EQUALS(pal[green][2], 0);

		const int blue = 5;
		TS_ASSERT_EQUALS(pal[blue][0], 0);
		TS_ASSERT_EQUALS(pal[blue][1], 0);
		TS_ASSERT_EQUALS(pal[blue][2], 255);
	}

	void test_palette_levels_are_evenly_spaced_and_rise() {
		uint8_t pal[256][3];
		dosDitherPalette(pal);

		for (int i = 1; i < kDitherRed; i++)
			TS_ASSERT(pal[i * kDitherMulRed][0] > pal[(i - 1) * kDitherMulRed][0]);
		for (int i = 1; i < kDitherGreen; i++)
			TS_ASSERT(pal[i * kDitherMulGreen][1] > pal[(i - 1) * kDitherMulGreen][1]);
		for (int i = 1; i < kDitherBlue; i++)
			TS_ASSERT(pal[i][2] > pal[i - 1][2]);

		// Six levels of red: 0, 51, 102, 153, 204, 255
		for (int i = 0; i < kDitherRed; i++)
			TS_ASSERT_EQUALS(pal[i * kDitherMulRed][0], i * 51);
	}

	void test_the_last_four_entries_are_greys() {
		uint8_t pal[256][3];
		dosDitherPalette(pal);
		for (int i = kDitherColours; i < 256; i++) {
			TS_ASSERT_EQUALS(pal[i][0], pal[i][1]);
			TS_ASSERT_EQUALS(pal[i][1], pal[i][2]);
		}
		TS_ASSERT_EQUALS(pal[255][0], 255);
	}

	void test_every_cube_colour_is_different() {
		uint8_t pal[256][3];
		dosDitherPalette(pal);
		for (int i = 0; i < kDitherColours; i++) {
			for (int j = i + 1; j < kDitherColours; j++)
				TS_ASSERT(memcmp(pal[i], pal[j], 3) != 0);
		}
	}

	void test_the_matrix_holds_each_threshold_once() {
		bool seen[16] = { false };
		for (int i = 0; i < 16; i++) {
			TS_ASSERT(dosBayer4[i] < 16);
			TS_ASSERT(!seen[dosBayer4[i]]);
			seen[dosBayer4[i]] = true;
		}
	}

	void test_channel_values_spread_over_the_full_range() {
		TS_ASSERT_EQUALS(dosExpand8(0, 5), 0);
		TS_ASSERT_EQUALS(dosExpand8(31, 5), 255);                // all ones is 255, not 248
		TS_ASSERT_EQUALS(dosExpand8(63, 6), 255);
		TS_ASSERT_EQUALS(dosExpand8(1, 1), 255);
		TS_ASSERT_EQUALS(dosExpand8(0xA, 4), 0xAA);
		TS_ASSERT_EQUALS(dosExpand8(200, 8), 200);               // 8 bits are left alone
		TS_ASSERT_EQUALS(dosExpand8(16, 5), 132);                // the top bit of five, repeated: 10000 10000 10 -> 10000100
	}

	void test_expanding_never_goes_backwards() {
		for (int bits = 1; bits <= 8; bits++) {
			int last = -1;
			for (int v = 0; v < (1 << bits); v++) {
				const int e = dosExpand8(v, bits);
				TS_ASSERT(e >= last);
				TS_ASSERT(e <= 255);
				last = e;
			}
		}
	}

	void test_black_and_white_are_the_same_level_in_every_cell() {
		for (int levels = 2; levels <= 8; levels++) {
			for (int cell = 0; cell < 16; cell++) {
				TS_ASSERT_EQUALS(dosDitherLevel(0, levels, cell), 0);
				TS_ASSERT_EQUALS(dosDitherLevel(255, levels, cell), levels - 1);
			}
		}
	}

	void test_levels_stay_in_range_and_rise_with_the_value() {
		for (int levels = 2; levels <= 8; levels++) {
			for (int cell = 0; cell < 16; cell++) {
				int last = 0;
				for (int v = 0; v < 256; v++) {
					const int level = dosDitherLevel(v, levels, cell);
					TS_ASSERT(level >= last);
					TS_ASSERT(level < levels);
					last = level;
				}
			}
		}
	}

	/**
	 * The point of dithering: over the 16 cells of the pattern the levels add up to what the value is,
	 * so a flat area looks the right brightness (Hermite's identity: the sum of floor(x + c/16 + 1/32)
	 * over c = 0..15 is floor(16 x + 1/2))
	 */
	void test_the_pattern_averages_to_the_value() {
		for (int levels = 6; levels <= 7; levels++) {
			for (int v = 0; v < 256; v++) {
				int sum = 0;
				for (int cell = 0; cell < 16; cell++)
					sum += dosDitherLevel(v, levels, dosBayer4[cell]);
				const int expected = (32 * v * (levels - 1) + 255) / 510;
				TS_ASSERT_EQUALS(sum, expected);
			}
		}
	}

	void test_plain_level_is_the_nearest() {
		TS_ASSERT_EQUALS(dosPlainLevel(0, 6), 0);
		TS_ASSERT_EQUALS(dosPlainLevel(255, 6), 5);
		TS_ASSERT_EQUALS(dosPlainLevel(127, 6), 2);              // 127 * 5 / 255 = 2.49: nearest 2
		TS_ASSERT_EQUALS(dosPlainLevel(128, 6), 3);              // 128 * 5 / 255 = 2.51: nearest 3
		TS_ASSERT_EQUALS(dosPlainLevel(51, 6), 1);
		TS_ASSERT_EQUALS(dosPlainLevel(76, 6), 1);               // 1.49
		TS_ASSERT_EQUALS(dosPlainLevel(77, 6), 2);               // 1.51
	}

	void test_channel_tables_for_565() {
		const Tables t(5, 6, 5);
		for (int cell = 0; cell < 16; cell++) {
			TS_ASSERT_EQUALS(t.r[cell * 256 + 0], 0);
			TS_ASSERT_EQUALS(t.r[cell * 256 + 31], 5 * kDitherMulRed);      // full red is the top level
			TS_ASSERT_EQUALS(t.g[cell * 256 + 63], 6 * kDitherMulGreen);
			TS_ASSERT_EQUALS(t.b[cell * 256 + 31], 5);
		}
	}

	void test_values_past_the_channel_width_count_as_the_largest() {
		const Tables t(5, 6, 5);
		for (int cell = 0; cell < 16; cell++) {
			TS_ASSERT_EQUALS(t.r[cell * 256 + 200], t.r[cell * 256 + 31]);
			TS_ASSERT_EQUALS(t.g[cell * 256 + 255], t.g[cell * 256 + 63]);
		}
	}

	void test_plain_tables() {
		uint8_t table[256];
		dosPlainChannelTable(table, 8, kDitherRed, kDitherMulRed);
		TS_ASSERT_EQUALS(table[0], 0);
		TS_ASSERT_EQUALS(table[255], 5 * kDitherMulRed);
		TS_ASSERT_EQUALS(table[128], 3 * kDitherMulRed);

		dosPlainChannelTable(table, 5, kDitherBlue, kDitherMulBlue);
		TS_ASSERT_EQUALS(table[31], 5);
		TS_ASSERT_EQUALS(table[100], 5);
	}

	/** A pixel through the tables lands on the palette entry of the colour it is */
	void test_white_and_black_pixels_pick_white_and_black() {
		uint8_t pal[256][3];
		dosDitherPalette(pal);
		const Tables t(5, 6, 5);

		for (int cell = 0; cell < 16; cell++) {
			const int white = t.r[cell * 256 + 31] + t.g[cell * 256 + 63] + t.b[cell * 256 + 31];
			TS_ASSERT_EQUALS(pal[white][0], 255);
			TS_ASSERT_EQUALS(pal[white][1], 255);
			TS_ASSERT_EQUALS(pal[white][2], 255);

			const int black = t.r[cell * 256] + t.g[cell * 256] + t.b[cell * 256];
			TS_ASSERT_EQUALS(black, 0);
		}
	}

	/** A flat mid grey is a mix of two neighbouring greys over the pattern, not a band */
	void test_a_flat_area_uses_two_neighbouring_levels() {
		const Tables t(5, 6, 5);
		bool low = false, high = false;
		for (int cell = 0; cell < 16; cell++) {
			const int r = t.r[cell * 256 + 16] / kDitherMulRed;       // 16 of 31: about half
			TS_ASSERT(r == 2 || r == 3);
			if (r == 2)
				low = true;
			else
				high = true;
		}
		TS_ASSERT(low && high);
	}

	/** The unrolled line converter must give the plain one's bytes for any pixels, any start and any length */
	void checkRows(int rs, int gs, int bs, unsigned rm, unsigned gm, unsigned bm, const Tables &t) {
		unsigned state = 12345;
		for (int row = 0; row < 4; row++) {
			const uint8_t *rt = t.r + row * 4 * 256, *gt = t.g + row * 4 * 256, *bt = t.b + row * 4 * 256;
			for (int x0 = 0; x0 < 9; x0++) {
				for (int length = 0; length <= 41; length++) {
					uint16_t pixels[64];
					for (int i = 0; i < 64; i++)
						pixels[i] = (uint16_t)nextRandom(state);

					uint8_t fast[80], slow[80];
					memset(fast, 0xAA, sizeof(fast));
					memset(slow, 0xAA, sizeof(slow));
					dosDitherRow(fast, pixels, x0, x0 + length, rt, gt, bt, rs, gs, bs, rm, gm, bm);
					dosDitherRowReference(slow, pixels, x0, x0 + length, rt, gt, bt, rs, gs, bs, rm, gm, bm);
					TS_ASSERT_EQUALS(memcmp(fast, slow, sizeof(fast)), 0);   // same bytes, and nothing written outside [x0, x1)
					if (memcmp(fast, slow, sizeof(fast)) != 0)
						return;
				}
			}
		}
	}

	void test_the_unrolled_line_converter_matches_the_plain_one_in_565() {
		const Tables t(5, 6, 5);
		checkRows(11, 5, 0, 31, 63, 31, t);
	}

	void test_the_unrolled_line_converter_matches_the_plain_one_in_555() {
		const Tables t(5, 5, 5);
		checkRows(10, 5, 0, 31, 31, 31, t);
	}

	void test_the_unrolled_line_converter_matches_the_plain_one_in_bgr_565() {
		const Tables t(5, 6, 5);
		checkRows(0, 5, 11, 31, 63, 31, t);
	}

	void test_the_line_converter_leaves_pixels_outside_the_range_alone() {
		const Tables t(5, 6, 5);
		uint16_t pixels[8] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
		uint8_t line[16];
		memset(line, 0x55, sizeof(line));
		dosDitherRow(line, pixels, 5, 5, t.r, t.g, t.b, 11, 5, 0, 31, 63, 31);   // an empty range
		for (int i = 0; i < 16; i++)
			TS_ASSERT_EQUALS(line[i], 0x55);

		dosDitherRow(line, pixels, 3, 8, t.r, t.g, t.b, 11, 5, 0, 31, 63, 31);
		for (int i = 0; i < 3; i++)
			TS_ASSERT_EQUALS(line[i], 0x55);
		for (int i = 8; i < 16; i++)
			TS_ASSERT_EQUALS(line[i], 0x55);
		for (int i = 3; i < 8; i++)
			TS_ASSERT_EQUALS(line[i], 251);                      // white pixels give the white of the palette
	}
	void test_the_overlay_palette_spans_the_dac_range() {
		uint8 r, g, b;
		dosPalette332(0x00, r, g, b);
		TS_ASSERT_EQUALS(r, 0);
		TS_ASSERT_EQUALS(g, 0);
		TS_ASSERT_EQUALS(b, 0);
		dosPalette332(0xFF, r, g, b);                 // white: the DAC's 63 in each
		TS_ASSERT_EQUALS(r, 63);
		TS_ASSERT_EQUALS(g, 63);
		TS_ASSERT_EQUALS(b, 63);
		dosPalette332(0xE0, r, g, b);                 // red alone
		TS_ASSERT_EQUALS(r, 63);
		TS_ASSERT_EQUALS(g, 0);
		TS_ASSERT_EQUALS(b, 0);
		dosPalette332(0x1C, r, g, b);                 // green alone
		TS_ASSERT_EQUALS(g, 63);
		dosPalette332(0x03, r, g, b);                 // blue alone
		TS_ASSERT_EQUALS(b, 63);
		dosPalette332(0x49, r, g, b);                 // 2-2-1
		TS_ASSERT_EQUALS(r, 18);
		TS_ASSERT_EQUALS(g, 18);
		TS_ASSERT_EQUALS(b, 21);
	}

	void test_colours_to_the_overlay_palette() {
		TS_ASSERT_EQUALS(dosRgbTo332(0, 0, 0), 0x00);
		TS_ASSERT_EQUALS(dosRgbTo332(255, 255, 255), 0xFF);
		TS_ASSERT_EQUALS(dosRgbTo332(255, 0, 0), 0xE0);
		TS_ASSERT_EQUALS(dosRgbTo332(0, 255, 0), 0x1C);
		TS_ASSERT_EQUALS(dosRgbTo332(0, 0, 255), 0x03);
		TS_ASSERT_EQUALS(dosRgbTo332(31, 31, 63), 0x00); // just below the first step of each
		TS_ASSERT_EQUALS(dosRgbTo332(32, 32, 64), 0x25);
	}

	void test_each_overlay_colour_maps_back_to_its_own_index() {
		// What the screenshot and the cursor do: a palette entry, widened to 8 bits, finds itself again
		for (int i = 0; i < 256; i++) {
			uint8 r, g, b;
			dosPalette332(i, r, g, b);
			TS_ASSERT_EQUALS(dosRgbTo332(r * 4, g * 4, b * 4), i);
		}
	}

};
