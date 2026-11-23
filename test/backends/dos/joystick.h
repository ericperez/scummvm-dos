#include <cxxtest/TestSuite.h>

#include "backends/platform/dos/dos-joystick.h"

/**
 * The joystick of the DOS port, the arithmetic part: the buttons in a game port reading, whether the
 * readings of a resting stick are those of a stick, and how far a stick position moves the cursor.
 * See dos-joystick.h.
 */
class DosJoystickTestSuite : public CxxTest::TestSuite {
public:
	void test_buttons_are_the_low_two_of_the_high_nibble_and_active_low() {
		TS_ASSERT_EQUALS(dosJoyButtons(0xFF), 0);                // nothing pressed: all bits high
		TS_ASSERT_EQUALS(dosJoyButtons(0xEF), 1);                // bit 4 low: button 1
		TS_ASSERT_EQUALS(dosJoyButtons(0xDF), 2);                // bit 5 low: button 2
		TS_ASSERT_EQUALS(dosJoyButtons(0xCF), 3);                // both
	}

	void test_the_second_sticks_buttons_and_the_axis_bits_are_ignored() {
		TS_ASSERT_EQUALS(dosJoyButtons(0x3F), 0);                // buttons 3 and 4 (the second stick)
		TS_ASSERT_EQUALS(dosJoyButtons(0xF0), 0);                // axes running
		TS_ASSERT_EQUALS(dosJoyButtons(0xFC), 0);
		TS_ASSERT_EQUALS(dosJoyButtons(0x00), 3);                // everything low: both of the first two pressed
		TS_ASSERT_EQUALS(dosJoyButtons(0xEF | 0x0F), 1);
	}

	void test_a_steady_centred_stick_is_calibrated() {
		int cx = 0, cy = 0;
		TS_ASSERT(dosJoyCalibrated(600, 780, 98, 102, 128, 132, 6, cx, cy));
		TS_ASSERT_EQUALS(cx, 100);
		TS_ASSERT_EQUALS(cy, 130);
	}

	void test_readings_that_scatter_are_not_a_stick() {
		int cx = 0, cy = 0;
		// Centre 100, but the readings range over 60..140: nothing is attached
		TS_ASSERT(!dosJoyCalibrated(600, 600, 60, 140, 98, 102, 6, cx, cy));
		TS_ASSERT(!dosJoyCalibrated(600, 600, 98, 102, 60, 140, 6, cx, cy));
	}

	void test_the_spread_may_be_a_quarter_of_the_centre() {
		int cx = 0, cy = 0;
		TS_ASSERT(dosJoyCalibrated(600, 600, 90, 110, 90, 110, 6, cx, cy));      // 20 * 4 = 80 <= 100
		TS_ASSERT(dosJoyCalibrated(600, 600, 88, 113, 90, 110, 6, cx, cy));      // 25 * 4 = 100: still fine
		TS_ASSERT(!dosJoyCalibrated(600, 600, 87, 113, 90, 110, 6, cx, cy));     // 26 * 4 = 104: too much
	}

	void test_a_centre_too_small_to_measure_is_refused() {
		int cx = 0, cy = 0;
		TS_ASSERT(!dosJoyCalibrated(60, 600, 10, 10, 100, 100, 6, cx, cy));      // x centre 10
		TS_ASSERT(!dosJoyCalibrated(600, 60, 100, 100, 10, 10, 6, cx, cy));      // y centre 10
		TS_ASSERT(dosJoyCalibrated(90, 90, 15, 15, 15, 15, 6, cx, cy));          // 15 is the least
		TS_ASSERT(!dosJoyCalibrated(84, 84, 14, 14, 14, 14, 6, cx, cy));
	}

	void test_no_readings_is_not_a_stick() {
		int cx = 0, cy = 0;
		TS_ASSERT(!dosJoyCalibrated(0, 0, 0, 0, 0, 0, 0, cx, cy));
	}

	void test_a_stick_at_rest_does_not_move_the_cursor() {
		int fraction = 0;
		TS_ASSERT_EQUALS(dosJoyAxisMove(100, 100, fraction, 640, 33), 0);
		TS_ASSERT_EQUALS(fraction, 0);
	}

	void test_the_dead_zone_is_a_fifth_of_the_travel() {
		int fraction = 500;                                      // left over from before: forgotten at rest
		TS_ASSERT_EQUALS(dosJoyAxisMove(119, 100, fraction, 640, 100), 0);   // 190 thousandths
		TS_ASSERT_EQUALS(fraction, 0);
		TS_ASSERT_EQUALS(dosJoyAxisMove(81, 100, fraction, 640, 100), 0);
		TS_ASSERT(dosJoyAxisMove(125, 100, fraction, 640, 100) != 0 || fraction != 0);   // 250: out of the zone
	}

	void test_full_deflection_crosses_the_screen_in_a_second() {
		int fraction = 0;
		TS_ASSERT_EQUALS(dosJoyAxisMove(200, 100, fraction, 640, 1000), 640);    // all the way right
		TS_ASSERT_EQUALS(fraction, 0);
		TS_ASSERT_EQUALS(dosJoyAxisMove(0, 100, fraction, 480, 1000), -480);     // all the way left, on a 480 line screen
	}

	void test_beyond_full_deflection_is_full_deflection() {
		int fraction = 0;
		TS_ASSERT_EQUALS(dosJoyAxisMove(500, 100, fraction, 640, 1000), 640);
		TS_ASSERT_EQUALS(dosJoyAxisMove(-300, 100, fraction, 640, 1000), -640);
	}

	void test_small_deflections_are_damped() {
		int fraction = 0;
		// 600 thousandths: 400 beyond the zone of 800, half: squared is a quarter of full speed
		TS_ASSERT_EQUALS(dosJoyAxisMove(160, 100, fraction, 640, 1000), 160);
		fraction = 0;
		TS_ASSERT_EQUALS(dosJoyAxisMove(40, 100, fraction, 640, 1000), -160);    // the same on the other side
	}

	void test_speed_rises_with_the_deflection() {
		int last = 0;
		for (int raw = 120; raw <= 200; raw += 5) {
			int fraction = 0;
			const int move = dosJoyAxisMove(raw, 100, fraction, 640, 1000);
			TS_ASSERT(move >= last);
			last = move;
		}
	}

	void test_little_steps_add_up_through_the_fraction() {
		// 600 thousandths at 10 ms a step is 1.6 pixels: 16 pixels in ten steps, none lost
		int fraction = 0, total = 0;
		for (int i = 0; i < 10; i++)
			total += dosJoyAxisMove(160, 100, fraction, 640, 10);
		TS_ASSERT_EQUALS(total, 16);
		TS_ASSERT_EQUALS(fraction, 0);
	}

	void test_the_same_going_the_other_way() {
		int fraction = 0, total = 0;
		for (int i = 0; i < 10; i++)
			total += dosJoyAxisMove(40, 100, fraction, 640, 10);
		TS_ASSERT_EQUALS(total, -16);
		TS_ASSERT_EQUALS(fraction, 0);
	}

	void test_no_time_no_movement() {
		int fraction = 0;
		TS_ASSERT_EQUALS(dosJoyAxisMove(200, 100, fraction, 640, 0), 0);
	}

	void test_a_bad_centre_does_not_divide_by_zero() {
		int fraction = 0;
		TS_ASSERT_EQUALS(dosJoyAxisMove(50, 0, fraction, 640, 33), 0);
		TS_ASSERT_EQUALS(dosJoyAxisMove(50, -5, fraction, 640, 33), 0);
	}
};
