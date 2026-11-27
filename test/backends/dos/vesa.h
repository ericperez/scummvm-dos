#include <cxxtest/TestSuite.h>

#include "backends/platform/dos/dos-vesainfo.h"

/**
 * The VESA BIOS Extension blocks as the DOS port reads them: the fields of a mode, which modes it can
 * use, the colour fields of high colour modes, how many display pages fit in video memory, and the
 * banks a run of bytes crosses when there is no linear framebuffer. See dos-vesainfo.h.
 */
class DosVesaTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
		memset(_block, 0, sizeof(_block));
	}

	void test_mode_information_block_fields() {
		// A 640x480 256 colour mode of an S3 card: 64 KB banks at A000, linear framebuffer at E0000000
		WRITE_LE_UINT16(_block + 0x00, 0x009B);
		_block[0x02] = 0x07;
		WRITE_LE_UINT16(_block + 0x04, 64);
		WRITE_LE_UINT16(_block + 0x06, 64);
		WRITE_LE_UINT16(_block + 0x08, 0xA000);
		WRITE_LE_UINT16(_block + 0x10, 640);
		WRITE_LE_UINT16(_block + 0x12, 640);
		WRITE_LE_UINT16(_block + 0x14, 480);
		_block[0x19] = 8;
		_block[0x1B] = 4;
		WRITE_LE_UINT32(_block + 0x28, 0xE0000000);

		VesaModeInfo mi;
		dosVesaParseModeInfo(_block, mi);
		TS_ASSERT_EQUALS(mi.attributes, 0x009B);
		TS_ASSERT_EQUALS(mi.windowAAttributes, 0x07);
		TS_ASSERT_EQUALS(mi.granularityKB, 64);
		TS_ASSERT_EQUALS(mi.windowSizeKB, 64);
		TS_ASSERT_EQUALS(mi.windowASegment, 0xA000);
		TS_ASSERT_EQUALS(mi.pitch, 640);
		TS_ASSERT_EQUALS(mi.xResolution, 640);
		TS_ASSERT_EQUALS(mi.yResolution, 480);
		TS_ASSERT_EQUALS(mi.bitsPerPixel, 8);
		TS_ASSERT_EQUALS(mi.memoryModel, 4);
		TS_ASSERT_EQUALS(mi.physBase, 0xE0000000u);
		TS_ASSERT(dosVesaIs256ColourMode(mi));
		TS_ASSERT(dosVesaHasLinearFramebuffer(mi));
		TS_ASSERT(!dosVesaIsHighColourMode(mi));
	}

	void test_the_block_is_little_endian_byte_by_byte() {
		_block[0x12] = 0x20;   // 800 = 0x0320
		_block[0x13] = 0x03;
		_block[0x28] = 0x00;
		_block[0x29] = 0x00;
		_block[0x2A] = 0x00;
		_block[0x2B] = 0xFD;
		VesaModeInfo mi;
		dosVesaParseModeInfo(_block, mi);
		TS_ASSERT_EQUALS(mi.xResolution, 800);
		TS_ASSERT_EQUALS(mi.physBase, 0xFD000000u);
	}

	void test_modes_that_cannot_be_used() {
		VesaModeInfo mi = mode256();
		TS_ASSERT(dosVesaIs256ColourMode(mi));

		mi.attributes = kDosVesaModeGraphics;               // not supported by the monitor or card
		TS_ASSERT(!dosVesaIs256ColourMode(mi));
		mi.attributes = kDosVesaModeSupported;              // a text mode
		TS_ASSERT(!dosVesaIs256ColourMode(mi));

		mi = mode256();
		mi.memoryModel = 3;                                 // 8 bits but planar
		TS_ASSERT(!dosVesaIs256ColourMode(mi));
		mi = mode256();
		mi.bitsPerPixel = 4;                                // 16 colours
		TS_ASSERT(!dosVesaIs256ColourMode(mi));
	}

	void test_high_colour_modes() {
		VesaModeInfo mi = mode256();
		mi.memoryModel = kDosVesaDirectColour;
		mi.bitsPerPixel = 16;
		TS_ASSERT(dosVesaIsHighColourMode(mi));
		TS_ASSERT(!dosVesaIs256ColourMode(mi));
		mi.bitsPerPixel = 15;
		TS_ASSERT(dosVesaIsHighColourMode(mi));
		mi.bitsPerPixel = 24;                               // three bytes a pixel
		TS_ASSERT(!dosVesaIsHighColourMode(mi));
		mi.bitsPerPixel = 32;
		TS_ASSERT(!dosVesaIsHighColourMode(mi));
		mi.bitsPerPixel = 16;
		mi.memoryModel = kDosVesaPackedPixel;
		TS_ASSERT(!dosVesaIsHighColourMode(mi));
		mi.memoryModel = kDosVesaDirectColour;
		mi.attributes = kDosVesaModeGraphics;
		TS_ASSERT(!dosVesaIsHighColourMode(mi));
	}

	void test_a_linear_framebuffer_needs_the_flag_and_an_address() {
		VesaModeInfo mi = mode256();
		TS_ASSERT(dosVesaHasLinearFramebuffer(mi));
		mi.physBase = 0;                                    // a VBE 1.2 BIOS sets the flag without one
		TS_ASSERT(!dosVesaHasLinearFramebuffer(mi));
		mi = mode256();
		mi.attributes &= ~kDosVesaModeLinear;
		TS_ASSERT(!dosVesaHasLinearFramebuffer(mi));
	}

	void test_colour_fields_the_bios_describes() {
		setFields(5, 11, 6, 5, 5, 0);
		byte rs, rp, gs, gp, bs, bp;
		TS_ASSERT(dosVesaDirectFields(_block, 16, rs, rp, gs, gp, bs, bp));
		TS_ASSERT_EQUALS(rs, 5);
		TS_ASSERT_EQUALS(rp, 11);
		TS_ASSERT_EQUALS(gs, 6);
		TS_ASSERT_EQUALS(gp, 5);
		TS_ASSERT_EQUALS(bs, 5);
		TS_ASSERT_EQUALS(bp, 0);

		setFields(5, 0, 5, 5, 5, 10);                     // blue on top: BGR555
		TS_ASSERT(dosVesaDirectFields(_block, 15, rs, rp, gs, gp, bs, bp));
		TS_ASSERT_EQUALS(rp, 0);
		TS_ASSERT_EQUALS(bp, 10);
	}

	void test_colour_fields_the_bios_leaves_out() {
		byte rs, rp, gs, gp, bs, bp;
		TS_ASSERT(dosVesaDirectFields(_block, 16, rs, rp, gs, gp, bs, bp)); // RGB565
		TS_ASSERT_EQUALS(rs, 5);
		TS_ASSERT_EQUALS(rp, 11);
		TS_ASSERT_EQUALS(gs, 6);
		TS_ASSERT_EQUALS(gp, 5);
		TS_ASSERT_EQUALS(bs, 5);
		TS_ASSERT_EQUALS(bp, 0);

		TS_ASSERT(dosVesaDirectFields(_block, 15, rs, rp, gs, gp, bs, bp)); // RGB555
		TS_ASSERT_EQUALS(rs, 5);
		TS_ASSERT_EQUALS(rp, 10);
		TS_ASSERT_EQUALS(gs, 5);
		TS_ASSERT_EQUALS(gp, 5);
		TS_ASSERT_EQUALS(bs, 5);
		TS_ASSERT_EQUALS(bp, 0);
	}

	void test_colour_fields_the_port_cannot_convert_to() {
		byte rs, rp, gs, gp, bs, bp;
		setFields(4, 8, 4, 4, 4, 0);                      // 12-bit colour
		TS_ASSERT(!dosVesaDirectFields(_block, 16, rs, rp, gs, gp, bs, bp));
		setFields(9, 18, 9, 9, 9, 0);                     // more than 8 bits a field
		TS_ASSERT(!dosVesaDirectFields(_block, 16, rs, rp, gs, gp, bs, bp));
		setFields(8, 16, 8, 8, 8, 0);                     // 8 bits each is still fine
		TS_ASSERT(dosVesaDirectFields(_block, 16, rs, rp, gs, gp, bs, bp));
		setFields(5, 11, 0, 5, 5, 0);                     // a field missing, the others described
		TS_ASSERT(!dosVesaDirectFields(_block, 16, rs, rp, gs, gp, bs, bp));
	}

	void test_real_mode_far_pointers() {
		TS_ASSERT_EQUALS(dosVesaRealPointer(0xC0000100), 0xC0100u);   // C000:0100
		TS_ASSERT_EQUALS(dosVesaRealPointer(0x12345678), 0x179B8u);   // 1234:5678
		TS_ASSERT_EQUALS(dosVesaRealPointer(0), 0u);
		TS_ASSERT_EQUALS(dosVesaRealPointer(0xFFFFFFFF), 0x10FFEFu);  // FFFF:FFFF, past 1 MB
	}

	void test_video_memory_in_64_kb_blocks() {
		WRITE_LE_UINT16(_block + 18, 16);                  // a 1 MB card
		TS_ASSERT_EQUALS(dosVesaMemoryKB(_block), 1024);
		WRITE_LE_UINT16(_block + 18, 0x40);                // 4 MB
		TS_ASSERT_EQUALS(dosVesaMemoryKB(_block), 4096);
		WRITE_LE_UINT16(_block + 18, 0);
		TS_ASSERT_EQUALS(dosVesaMemoryKB(_block), 0);
	}

	void test_display_pages() {
		TS_ASSERT_EQUALS(dosVesaPages(1024, 640, 480, 3), 3);    // 300 KB a page: 3 in 1 MB
		TS_ASSERT_EQUALS(dosVesaPages(1024, 640, 480, 2), 2);    // no more than asked for
		TS_ASSERT_EQUALS(dosVesaPages(512, 640, 480, 3), 1);     // only one fits in 512 KB
		TS_ASSERT_EQUALS(dosVesaPages(1024, 1600, 600, 3), 1);   // 800x600 high colour: 937 KB a page
		TS_ASSERT_EQUALS(dosVesaPages(2048, 1600, 600, 3), 2);
		TS_ASSERT_EQUALS(dosVesaPages(4096, 320, 200, 3), 3);
	}

	void test_display_pages_without_enough_to_go_on() {
		TS_ASSERT_EQUALS(dosVesaPages(4096, 640, 480, 1), 1);    // flipping not wanted
		TS_ASSERT_EQUALS(dosVesaPages(4096, 640, 480, 0), 1);
		TS_ASSERT_EQUALS(dosVesaPages(0, 640, 480, 3), 1);       // the card does not say
		TS_ASSERT_EQUALS(dosVesaPages(-1, 640, 480, 3), 1);
		TS_ASSERT_EQUALS(dosVesaPages(4096, 0, 480, 3), 1);      // no division by zero
		TS_ASSERT_EQUALS(dosVesaPages(4096, 640, 0, 3), 1);
	}

	void test_a_run_inside_one_bank() {
		uint bank, inWindow;
		TS_ASSERT_EQUALS(dosVesaBankSpan(1000, 640, 65536, 65536, bank, inWindow), 640u);
		TS_ASSERT_EQUALS(bank, 0u);
		TS_ASSERT_EQUALS(inWindow, 1000u);

		TS_ASSERT_EQUALS(dosVesaBankSpan(65536 * 3 + 5, 100, 65536, 65536, bank, inWindow), 100u);
		TS_ASSERT_EQUALS(bank, 3u);
		TS_ASSERT_EQUALS(inWindow, 5u);
	}

	void test_a_run_across_two_banks() {
		// Row 102 of 640x480: starts at 65280, 256 bytes before the end of bank 0
		uint bank, inWindow;
		unsigned long offset = 102ul * 640;
		uint left = 640;
		TS_ASSERT_EQUALS(dosVesaBankSpan(offset, left, 65536, 65536, bank, inWindow), 256u);
		TS_ASSERT_EQUALS(bank, 0u);
		TS_ASSERT_EQUALS(inWindow, 65280u);
		offset += 256;
		left -= 256;
		TS_ASSERT_EQUALS(dosVesaBankSpan(offset, left, 65536, 65536, bank, inWindow), 384u);
		TS_ASSERT_EQUALS(bank, 1u);
		TS_ASSERT_EQUALS(inWindow, 0u);
	}

	void test_a_fine_granularity_moves_the_window_to_the_run() {
		// Cirrus Logic and others: 4 KB granularity, a 64 KB window. The bank starts at the 4 KB step
		// below the run, so most runs fit without a switch
		uint bank, inWindow;
		TS_ASSERT_EQUALS(dosVesaBankSpan(65280, 640, 4096, 65536, bank, inWindow), 640u);
		TS_ASSERT_EQUALS(bank, 15u);
		TS_ASSERT_EQUALS(inWindow, 65280u - 15 * 4096);
	}

	void test_every_byte_of_a_screen_is_copied_once_in_banks() {
		// Copy a 800x600 screen with a pitch of 832 through 64 KB banks and count what lands where
		const uint pitch = 832, height = 600, width = 800;
		unsigned long copied = 0;
		uint switches = 0, lastBank = 0xFFFFFFFF;
		for (uint row = 0; row < height; row++) {
			unsigned long offset = (unsigned long)row * pitch;
			uint left = width;
			while (left > 0) {
				uint bank, inWindow;
				const uint n = dosVesaBankSpan(offset, left, 65536, 65536, bank, inWindow);
				TS_ASSERT_LESS_THAN(0u, n);
				TS_ASSERT_LESS_THAN_EQUALS(inWindow + n, 65536u);
				TS_ASSERT_EQUALS((unsigned long)bank * 65536 + inWindow, offset);
				if (bank != lastBank) {
					switches++;
					lastBank = bank;
				}
				copied += n;
				offset += n;
				left -= n;
			}
		}
		TS_ASSERT_EQUALS(copied, (unsigned long)width * height);
		TS_ASSERT_EQUALS(switches, (pitch * (height - 1) + width - 1) / 65536 + 1);
	}

private:
	byte _block[256];

	static VesaModeInfo mode256() {
		VesaModeInfo mi;
		memset(&mi, 0, sizeof(mi));
		mi.attributes = kDosVesaModeSupported | kDosVesaModeGraphics | kDosVesaModeLinear;
		mi.bitsPerPixel = 8;
		mi.memoryModel = kDosVesaPackedPixel;
		mi.xResolution = 640;
		mi.yResolution = 480;
		mi.physBase = 0xE0000000;
		return mi;
	}

	void setFields(byte rs, byte rp, byte gs, byte gp, byte bs, byte bp) {
		_block[0x1F] = rs;
		_block[0x20] = rp;
		_block[0x21] = gs;
		_block[0x22] = gp;
		_block[0x23] = bs;
		_block[0x24] = bp;
	}
};
