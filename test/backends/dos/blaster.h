#include <cxxtest/TestSuite.h>

#include "backends/platform/dos/dos-blaster.h"

/**
 * The Sound Blaster settings of the DOS port: the BLASTER variable the card's driver sets (port, IRQ,
 * DMA channels, MIDI port, type), MIDI port numbers, and the sample rates the DSP can play at in 8-bit
 * and 16-bit mode. See dos-blaster.h.
 */
class DosBlasterTestSuite : public CxxTest::TestSuite {
public:
	void test_a_full_sb16_variable() {
		const DosBlaster b = dosParseBlaster("A220 I5 D1 H5 P330 T6");
		TS_ASSERT_EQUALS(b.base, 0x220u);
		TS_ASSERT_EQUALS(b.irq, 5);
		TS_ASSERT_EQUALS(b.dma, 1);
		TS_ASSERT_EQUALS(b.hdma, 5);
		TS_ASSERT_EQUALS(b.mpu, 0x330u);
		TS_ASSERT_EQUALS(b.type, 6u);
	}

	void test_settings_not_given_keep_their_defaults() {
		const DosBlaster b = dosParseBlaster("A240 I7 D3 T4"); // an SB Pro: no 16-bit channel, no MIDI port
		TS_ASSERT_EQUALS(b.base, 0x240u);
		TS_ASSERT_EQUALS(b.irq, 7);
		TS_ASSERT_EQUALS(b.dma, 3);
		TS_ASSERT_EQUALS(b.hdma, -1);
		TS_ASSERT_EQUALS(b.mpu, 0u);
		TS_ASSERT_EQUALS(b.type, 4u);
	}

	void test_no_variable_at_all() {
		const DosBlaster none = dosParseBlaster(nullptr);
		TS_ASSERT_EQUALS(none.base, 0u);
		TS_ASSERT_EQUALS(none.irq, -1);
		TS_ASSERT_EQUALS(none.dma, -1);
		TS_ASSERT_EQUALS(none.hdma, -1);
		TS_ASSERT_EQUALS(none.mpu, 0u);
		TS_ASSERT_EQUALS(none.type, 0u);

		const DosBlaster empty = dosParseBlaster("");
		TS_ASSERT_EQUALS(empty.base, 0u);
		TS_ASSERT_EQUALS(empty.irq, -1);
		const DosBlaster spaces = dosParseBlaster("   ");
		TS_ASSERT_EQUALS(spaces.dma, -1);
	}

	void test_any_case_and_extra_spaces() {
		const DosBlaster b = dosParseBlaster("  a2a0  i10 d0   h7 p300 t6 ");
		TS_ASSERT_EQUALS(b.base, 0x2A0u);
		TS_ASSERT_EQUALS(b.irq, 10);
		TS_ASSERT_EQUALS(b.dma, 0);   // channel 0 is a channel, not "none"
		TS_ASSERT_EQUALS(b.hdma, 7);
		TS_ASSERT_EQUALS(b.mpu, 0x300u);
		TS_ASSERT_EQUALS(b.type, 6u);
	}

	void test_ports_are_hexadecimal_and_the_rest_decimal() {
		const DosBlaster b = dosParseBlaster("A2C0 I11 D1 PFF0");
		TS_ASSERT_EQUALS(b.base, 0x2C0u);
		TS_ASSERT_EQUALS(b.irq, 11);   // not 0x11
		TS_ASSERT_EQUALS(b.mpu, 0xFF0u);
		const DosBlaster c = dosParseBlaster("IA DB");
		TS_ASSERT_EQUALS(c.irq, -1);   // hexadecimal digits are no decimal number
		TS_ASSERT_EQUALS(c.dma, -1);
	}

	void test_unknown_fields_are_passed_over() {
		// E (EMU8000 port) and M (mixer port) come from AWE32 and SB16 drivers
		const DosBlaster b = dosParseBlaster("A220 E620 I5 M220 D1 H5 T6");
		TS_ASSERT_EQUALS(b.base, 0x220u);
		TS_ASSERT_EQUALS(b.irq, 5);
		TS_ASSERT_EQUALS(b.dma, 1);
		TS_ASSERT_EQUALS(b.hdma, 5);
		TS_ASSERT_EQUALS(b.type, 6u);
	}

	void test_a_field_without_a_value_is_passed_over() {
		const DosBlaster b = dosParseBlaster("A220 H T4 I");
		TS_ASSERT_EQUALS(b.base, 0x220u);
		TS_ASSERT_EQUALS(b.hdma, -1);
		TS_ASSERT_EQUALS(b.irq, -1);
		TS_ASSERT_EQUALS(b.type, 4u);
	}

	void test_the_rest_of_a_field_after_its_number_is_ignored() {
		const DosBlaster b = dosParseBlaster("A220h I5x D1 T6");
		TS_ASSERT_EQUALS(b.base, 0x220u);
		TS_ASSERT_EQUALS(b.irq, 5);
		TS_ASSERT_EQUALS(b.dma, 1);
		TS_ASSERT_EQUALS(b.type, 6u);
	}

	void test_a_letter_inside_a_value_starts_no_field() {
		// The D in "A2D0" is part of the port, not a DMA channel
		const DosBlaster b = dosParseBlaster("A2D0 I5");
		TS_ASSERT_EQUALS(b.base, 0x2D0u);
		TS_ASSERT_EQUALS(b.dma, -1);
	}

	void test_the_last_of_two_fields_wins() {
		const DosBlaster b = dosParseBlaster("I5 I7");
		TS_ASSERT_EQUALS(b.irq, 7);
	}

	void test_blaster_numbers() {
		bool any;
		const char *p = "220 rest";
		TS_ASSERT_EQUALS(dosBlasterNumber(p, 16, any), 0x220u);
		TS_ASSERT(any);
		TS_ASSERT_EQUALS(*p, ' ');      // left on the first character after the number

		p = "aBc";
		TS_ASSERT_EQUALS(dosBlasterNumber(p, 16, any), 0xABCu);
		TS_ASSERT_EQUALS(*p, '\0');

		p = "15b";
		TS_ASSERT_EQUALS(dosBlasterNumber(p, 10, any), 15u);
		TS_ASSERT_EQUALS(*p, 'b');

		p = "x1";
		TS_ASSERT_EQUALS(dosBlasterNumber(p, 16, any), 0u);
		TS_ASSERT(!any);
		TS_ASSERT_EQUALS(*p, 'x');
	}

	void test_hexadecimal_port_numbers() {
		TS_ASSERT_EQUALS(dosParseHex("330"), 0x330u);
		TS_ASSERT_EQUALS(dosParseHex("300"), 0x300u);
		TS_ASSERT_EQUALS(dosParseHex("2a0"), 0x2A0u);
		TS_ASSERT_EQUALS(dosParseHex("2A0"), 0x2A0u);
		TS_ASSERT_EQUALS(dosParseHex("330h"), 0x330u); // up to the first character that is no digit
		TS_ASSERT_EQUALS(dosParseHex(""), 0u);
		TS_ASSERT_EQUALS(dosParseHex("port"), 0u);
		TS_ASSERT_EQUALS(dosParseHex(nullptr), 0u);
	}

	void test_8_bit_rates_come_from_a_time_constant() {
		uint rate;
		// 22050 Hz: 1000000 / 22050 is 45.4, so the divisor is 45 and the DSP plays at 22222 Hz
		TS_ASSERT_EQUALS(dosSbTimeConstant(22050, rate), 256 - 45);
		TS_ASSERT_EQUALS(rate, 22222u);
		// 11025 Hz: divisor 91, 10989 Hz
		TS_ASSERT_EQUALS(dosSbTimeConstant(11025, rate), 256 - 91);
		TS_ASSERT_EQUALS(rate, 10989u);
		// 8000 Hz exactly
		TS_ASSERT_EQUALS(dosSbTimeConstant(8000, rate), 256 - 125);
		TS_ASSERT_EQUALS(rate, 8000u);
	}

	void test_8_bit_rates_are_kept_to_what_the_dsp_can_play() {
		uint rate;
		TS_ASSERT_EQUALS(dosSbTimeConstant(44100, rate), 256 - 45);  // above 22222 Hz
		TS_ASSERT_EQUALS(rate, 22222u);
		TS_ASSERT_EQUALS(dosSbTimeConstant(1000, rate), 256 - 250);  // below 4000 Hz
		TS_ASSERT_EQUALS(rate, 4000u);
		TS_ASSERT_EQUALS(dosSbTimeConstant(0, rate), 256 - 250);     // no division by zero
		TS_ASSERT_EQUALS(rate, 4000u);
	}

	void test_8_bit_rates_round_to_the_nearest_divisor() {
		uint rate;
		// 1000000 / 15000 is 66.67: 67, not 66
		TS_ASSERT_EQUALS(dosSbTimeConstant(15000, rate), 256 - 67);
		TS_ASSERT_EQUALS(rate, 14925u);
		// For every rate the DSP plays at, the one asked for is at most half a divisor away
		for (uint wanted = 4000; wanted <= 22222; wanted += 97) {
			dosSbTimeConstant(wanted, rate);
			const uint divisor = 1000000 / rate;
			TS_ASSERT_LESS_THAN_EQUALS(1000000 / (divisor + 1), wanted);
			TS_ASSERT_LESS_THAN_EQUALS(wanted, 1000000 / (divisor - 1));
		}
	}

	void test_16_bit_rates_are_given_as_they_are() {
		TS_ASSERT_EQUALS(dosSb16Rate(22050), 22050u);
		TS_ASSERT_EQUALS(dosSb16Rate(44100), 44100u);
		TS_ASSERT_EQUALS(dosSb16Rate(11025), 11025u);
		TS_ASSERT_EQUALS(dosSb16Rate(5000), 5000u);
		TS_ASSERT_EQUALS(dosSb16Rate(48000), 44100u); // above what the SB16 plays
		TS_ASSERT_EQUALS(dosSb16Rate(4000), 5000u);   // below
		TS_ASSERT_EQUALS(dosSb16Rate(0), 5000u);
	}
};
