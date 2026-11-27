#include <cxxtest/TestSuite.h>

#include "backends/platform/dos/dos-scancodes.h"

/**
 * The keyboard controller's bytes on the DOS port: prefixes, releases, repeats, the modifier and lock
 * keys, and the key codes of the keys by their place, both from IRQ 1 and from the BIOS buffer when
 * IRQ 1 could not be hooked. See dos-scancodes.h.
 */
class DosScanCodesTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
		memset(&_state, 0, sizeof(_state));
	}

	void test_a_press_and_its_release() {
		DosKeyEvent ev = {};
		TS_ASSERT(dosScanDecode(_state, 0x1E, ev)); // A down
		TS_ASSERT_EQUALS(ev.scan, 0x1E);
		TS_ASSERT(!ev.extended);
		TS_ASSERT(ev.pressed);
		TS_ASSERT(!ev.repeat);
		TS_ASSERT_EQUALS(ev.ascii, 0);
		TS_ASSERT_EQUALS(ev.modifiers, 0);

		TS_ASSERT(dosScanDecode(_state, 0x9E, ev)); // A up: the release bit
		TS_ASSERT_EQUALS(ev.scan, 0x1E);
		TS_ASSERT(!ev.pressed);
		TS_ASSERT(!ev.repeat);
	}

	void test_a_key_held_down_repeats() {
		DosKeyEvent ev = {};
		dosScanDecode(_state, 0x1E, ev);
		TS_ASSERT(dosScanDecode(_state, 0x1E, ev)); // the typematic repeat
		TS_ASSERT(ev.pressed);
		TS_ASSERT(ev.repeat);
		dosScanDecode(_state, 0x9E, ev);
		TS_ASSERT(!ev.repeat);
		TS_ASSERT(dosScanDecode(_state, 0x1E, ev)); // a new press after the release
		TS_ASSERT(!ev.repeat);
	}

	void test_the_e0_prefix_marks_the_gray_keys() {
		DosKeyEvent ev = {};
		TS_ASSERT(!dosScanDecode(_state, 0xE0, ev)); // the prefix alone is no event
		TS_ASSERT(dosScanDecode(_state, 0x48, ev));  // gray Up
		TS_ASSERT_EQUALS(ev.scan, 0x48);
		TS_ASSERT(ev.extended);
		TS_ASSERT(dosScanDecode(_state, 0x48, ev));  // keypad 8: the same code without the prefix
		TS_ASSERT(!ev.extended);
		TS_ASSERT(!ev.repeat);                       // another key than the gray one
	}

	void test_a_gray_key_and_its_keypad_twin_are_down_apart() {
		DosKeyEvent ev = {};
		dosScanDecode(_state, 0xE0, ev);
		dosScanDecode(_state, 0x48, ev);  // gray Up down
		dosScanDecode(_state, 0xC8, ev);  // keypad 8 up (never down)
		dosScanDecode(_state, 0xE0, ev);
		TS_ASSERT(dosScanDecode(_state, 0x48, ev));
		TS_ASSERT(ev.repeat);             // gray Up is still down
	}

	void test_the_fake_shifts_around_gray_keys_are_dropped() {
		DosKeyEvent ev = {};
		// Print Screen: E0 2A E0 37, then E0 B7 E0 AA
		TS_ASSERT(!dosScanDecode(_state, 0xE0, ev));
		TS_ASSERT(!dosScanDecode(_state, 0x2A, ev));
		TS_ASSERT(!dosScanDecode(_state, 0xE0, ev));
		TS_ASSERT(dosScanDecode(_state, 0x37, ev));
		TS_ASSERT_EQUALS(ev.modifiers & kDosModShift, 0);
		TS_ASSERT(!dosScanDecode(_state, 0xE0, ev) && dosScanDecode(_state, 0xB7, ev));
		TS_ASSERT(!dosScanDecode(_state, 0xE0, ev));
		TS_ASSERT(!dosScanDecode(_state, 0xAA, ev));
		TS_ASSERT_EQUALS(_state.held, 0);

		// The same with the right Shift's code
		TS_ASSERT(!dosScanDecode(_state, 0xE0, ev));
		TS_ASSERT(!dosScanDecode(_state, 0x36, ev));
	}

	void test_the_pause_sequence_gives_nothing() {
		DosKeyEvent ev = {};
		static const uint8 pause[6] = { 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5 };
		for (int i = 0; i < 6; i++)
			TS_ASSERT(!dosScanDecode(_state, pause[i], ev));
		TS_ASSERT_EQUALS(_state.held, 0);  // its Ctrl codes are not the Ctrl key
		TS_ASSERT_EQUALS(_state.locks, 0); // nor its 0x45 Num Lock
		TS_ASSERT(dosScanDecode(_state, 0x1E, ev)); // the next key is decoded again
		TS_ASSERT_EQUALS(ev.scan, 0x1E);
	}

	void test_shift_ctrl_and_alt_are_held_left_or_right() {
		DosKeyEvent ev = {};
		dosScanDecode(_state, 0x2A, ev); // left Shift
		TS_ASSERT_EQUALS(ev.modifiers, kDosModShift);
		dosScanDecode(_state, 0x36, ev); // right Shift as well
		dosScanDecode(_state, 0xAA, ev); // left Shift up: right one still held
		TS_ASSERT_EQUALS(ev.modifiers, kDosModShift);
		dosScanDecode(_state, 0xB6, ev);
		TS_ASSERT_EQUALS(ev.modifiers, 0);

		dosScanDecode(_state, 0x1D, ev); // left Ctrl
		TS_ASSERT_EQUALS(ev.modifiers, kDosModCtrl);
		dosScanDecode(_state, 0x9D, ev);
		dosScanDecode(_state, 0xE0, ev);
		dosScanDecode(_state, 0x1D, ev); // right Ctrl
		TS_ASSERT_EQUALS(ev.modifiers, kDosModCtrl);
		TS_ASSERT_EQUALS(_state.held, kDosHeldRCtrl);
		dosScanDecode(_state, 0xE0, ev);
		dosScanDecode(_state, 0x9D, ev);
		TS_ASSERT_EQUALS(ev.modifiers, 0);

		dosScanDecode(_state, 0x38, ev); // left Alt
		TS_ASSERT_EQUALS(ev.modifiers, kDosModAlt);
		TS_ASSERT(!ev.rightAltOnly);
	}

	void test_altgr_alone_is_told_apart() {
		DosKeyEvent ev = {};
		dosScanDecode(_state, 0xE0, ev);
		dosScanDecode(_state, 0x38, ev); // right Alt (AltGr)
		TS_ASSERT_EQUALS(ev.modifiers, kDosModAlt);
		TS_ASSERT(ev.rightAltOnly);
		dosScanDecode(_state, 0x10, ev); // Q with AltGr: '@' on a German keyboard
		TS_ASSERT(ev.rightAltOnly);
		dosScanDecode(_state, 0x38, ev); // and the left Alt too
		TS_ASSERT(!ev.rightAltOnly);
	}

	void test_lock_keys_toggle_on_a_press() {
		DosKeyEvent ev = {};
		dosScanDecode(_state, 0x3A, ev); // Caps Lock down
		TS_ASSERT_EQUALS(ev.modifiers, kDosModCaps);
		dosScanDecode(_state, 0x3A, ev); // its repeat changes nothing
		TS_ASSERT_EQUALS(ev.modifiers, kDosModCaps);
		dosScanDecode(_state, 0xBA, ev); // up: still on
		TS_ASSERT_EQUALS(ev.modifiers, kDosModCaps);
		dosScanDecode(_state, 0x3A, ev); // the next press turns it off
		TS_ASSERT_EQUALS(ev.modifiers, 0);

		dosScanDecode(_state, 0x45, ev);
		TS_ASSERT_EQUALS(ev.modifiers, kDosModNum);
		dosScanDecode(_state, 0x46, ev);
		TS_ASSERT_EQUALS(ev.modifiers, kDosModNum | kDosModScroll);
	}

	void test_the_lock_keys_start_from_the_bios_flags() {
		TS_ASSERT_EQUALS(dosLocksFromBiosFlags(0), 0);
		TS_ASSERT_EQUALS(dosLocksFromBiosFlags(0x20), kDosModNum);   // Num Lock, as at boot
		TS_ASSERT_EQUALS(dosLocksFromBiosFlags(0x40), kDosModCaps);
		TS_ASSERT_EQUALS(dosLocksFromBiosFlags(0x10), kDosModScroll);
		TS_ASSERT_EQUALS(dosLocksFromBiosFlags(0x7F), kDosModCaps | kDosModNum | kDosModScroll);
		TS_ASSERT_EQUALS(dosLocksFromBiosFlags(0x0F), 0);            // Shift, Ctrl and Alt held are no locks

		_state.locks = dosLocksFromBiosFlags(0x20);
		DosKeyEvent ev = {};
		dosScanDecode(_state, 0x1E, ev);
		TS_ASSERT_EQUALS(ev.modifiers, kDosModNum);
	}

	void test_main_keys_by_place() {
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x01, false), Common::KEYCODE_ESCAPE);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x0E, false), Common::KEYCODE_BACKSPACE);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x0F, false), Common::KEYCODE_TAB);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x1C, false), Common::KEYCODE_RETURN);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x39, false), Common::KEYCODE_SPACE);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x02, false), Common::KEYCODE_1);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x0B, false), Common::KEYCODE_0);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x10, false), Common::KEYCODE_q);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x1E, false), Common::KEYCODE_a);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x2C, false), Common::KEYCODE_z);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x35, false), Common::KEYCODE_SLASH);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x2B, false), Common::KEYCODE_BACKSLASH);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x29, false), Common::KEYCODE_BACKQUOTE);
	}

	void test_every_typewriter_key_is_named_by_what_it_types() {
		for (uint8 scan = 0; scan < sizeof(kDosTypewriterKeys); scan++) {
			if (kDosTypewriterKeys[scan])
				TS_ASSERT_EQUALS(dosKeycodeForScan(scan, false), (Common::KeyCode)kDosTypewriterKeys[scan]);
		}
	}

	void test_modifier_and_lock_keys_by_place() {
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x2A, false), Common::KEYCODE_LSHIFT);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x36, false), Common::KEYCODE_RSHIFT);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x1D, false), Common::KEYCODE_LCTRL);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x1D, true), Common::KEYCODE_RCTRL);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x38, false), Common::KEYCODE_LALT);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x38, true), Common::KEYCODE_RALT);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x3A, false), Common::KEYCODE_CAPSLOCK);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x45, false), Common::KEYCODE_NUMLOCK);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x46, false), Common::KEYCODE_SCROLLOCK);
	}

	void test_function_keys() {
		for (int i = 0; i < 10; i++)
			TS_ASSERT_EQUALS(dosKeycodeForScan(0x3B + i, false), (Common::KeyCode)(Common::KEYCODE_F1 + i));
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x57, false), Common::KEYCODE_F11);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x58, false), Common::KEYCODE_F12);
	}

	void test_the_keypad_and_its_gray_twins() {
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x47, false), Common::KEYCODE_KP7);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x47, true), Common::KEYCODE_HOME);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x48, false), Common::KEYCODE_KP8);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x48, true), Common::KEYCODE_UP);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x49, true), Common::KEYCODE_PAGEUP);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x4B, true), Common::KEYCODE_LEFT);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x4D, true), Common::KEYCODE_RIGHT);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x4F, true), Common::KEYCODE_END);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x50, true), Common::KEYCODE_DOWN);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x51, true), Common::KEYCODE_PAGEDOWN);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x52, false), Common::KEYCODE_KP0);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x52, true), Common::KEYCODE_INSERT);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x53, false), Common::KEYCODE_KP_PERIOD);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x53, true), Common::KEYCODE_DELETE);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x1C, true), Common::KEYCODE_KP_ENTER);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x35, true), Common::KEYCODE_KP_DIVIDE);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x37, false), Common::KEYCODE_KP_MULTIPLY);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x37, true), Common::KEYCODE_PRINT);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x4A, false), Common::KEYCODE_KP_MINUS);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x4E, false), Common::KEYCODE_KP_PLUS);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x4C, false), Common::KEYCODE_KP5);
	}

	void test_keys_without_a_name() {
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x00, false), Common::KEYCODE_INVALID);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x54, false), Common::KEYCODE_INVALID); // SysRq
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x56, false), Common::KEYCODE_INVALID); // the 102nd key
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x7F, false), Common::KEYCODE_INVALID);
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x1E, true), Common::KEYCODE_INVALID);  // no gray A
		TS_ASSERT_EQUALS(dosKeycodeForScan(0x5B, true), Common::KEYCODE_INVALID);  // left Windows key
	}

	void test_modifier_flags() {
		TS_ASSERT_EQUALS(dosFlagsForModifiers(0), 0);
		TS_ASSERT_EQUALS(dosFlagsForModifiers(kDosModShift), Common::KBD_SHIFT);
		TS_ASSERT_EQUALS(dosFlagsForModifiers(kDosModCtrl), Common::KBD_CTRL);
		TS_ASSERT_EQUALS(dosFlagsForModifiers(kDosModAlt), Common::KBD_ALT);
		TS_ASSERT_EQUALS(dosFlagsForModifiers(kDosModCaps), Common::KBD_CAPS);
		TS_ASSERT_EQUALS(dosFlagsForModifiers(kDosModNum), Common::KBD_NUM);
		TS_ASSERT_EQUALS(dosFlagsForModifiers(kDosModScroll), Common::KBD_SCRL);
		TS_ASSERT_EQUALS(dosFlagsForModifiers(kDosModShift | kDosModAlt | kDosModNum),
		                 Common::KBD_SHIFT | Common::KBD_ALT | Common::KBD_NUM);
	}

	void test_bios_extended_keys() {
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x48), Common::KEYCODE_UP);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x50), Common::KEYCODE_DOWN);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x4B), Common::KEYCODE_LEFT);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x4D), Common::KEYCODE_RIGHT);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x47), Common::KEYCODE_HOME);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x4F), Common::KEYCODE_END);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x49), Common::KEYCODE_PAGEUP);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x51), Common::KEYCODE_PAGEDOWN);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x52), Common::KEYCODE_INSERT);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x53), Common::KEYCODE_DELETE);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x3B), Common::KEYCODE_F1);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x44), Common::KEYCODE_F10);
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x85), Common::KEYCODE_F11);  // the BIOS's own codes for F11
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x86), Common::KEYCODE_F12);  // and F12
		TS_ASSERT_EQUALS(dosBiosExtendedKey(0x1E), Common::KEYCODE_INVALID); // Alt+A: a letter, not a gray key
	}

	void test_letters_by_place() {
		TS_ASSERT_EQUALS(dosLetterForScan(0x10), 'q');
		TS_ASSERT_EQUALS(dosLetterForScan(0x19), 'p');
		TS_ASSERT_EQUALS(dosLetterForScan(0x1E), 'a');
		TS_ASSERT_EQUALS(dosLetterForScan(0x26), 'l');
		TS_ASSERT_EQUALS(dosLetterForScan(0x2C), 'z');
		TS_ASSERT_EQUALS(dosLetterForScan(0x32), 'm');
		TS_ASSERT_EQUALS(dosLetterForScan(0x0F), 0);  // Tab
		TS_ASSERT_EQUALS(dosLetterForScan(0x1A), 0);  // [
		TS_ASSERT_EQUALS(dosLetterForScan(0x27), 0);  // ;
		TS_ASSERT_EQUALS(dosLetterForScan(0x33), 0);  // ,
	}

	void test_bios_buffer_printable_characters() {
		Common::KeyState ks;
		TS_ASSERT(dosBiosKeyState(0x1E61, 0, 437, ks)); // a
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_a);
		TS_ASSERT_EQUALS(ks.ascii, 'a');
		TS_ASSERT_EQUALS(ks.flags, 0);

		TS_ASSERT(dosBiosKeyState(0x1E41, 0x02, 437, ks)); // A with left Shift: the key has no case
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_a);
		TS_ASSERT_EQUALS(ks.ascii, 'A');
		TS_ASSERT_EQUALS(ks.flags, Common::KBD_SHIFT);

		TS_ASSERT(dosBiosKeyState(0x0221, 0x01, 437, ks)); // ! with right Shift
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_EXCLAIM);
		TS_ASSERT_EQUALS(ks.ascii, '!');
	}

	void test_bios_buffer_control_keys() {
		Common::KeyState ks;
		TS_ASSERT(dosBiosKeyState(0x0E08, 0, 437, ks));
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_BACKSPACE);
		TS_ASSERT_EQUALS(ks.ascii, 8);
		TS_ASSERT(dosBiosKeyState(0x0F09, 0, 437, ks));
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_TAB);
		TS_ASSERT(dosBiosKeyState(0x1C0D, 0, 437, ks));
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_RETURN);
		TS_ASSERT_EQUALS(ks.ascii, 13);
		TS_ASSERT(dosBiosKeyState(0x011B, 0, 437, ks));
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_ESCAPE);
		TS_ASSERT_EQUALS(ks.ascii, 27);
	}

	void test_bios_buffer_ctrl_letters() {
		Common::KeyState ks;
		TS_ASSERT(dosBiosKeyState(0x2E03, 0x04, 437, ks)); // Ctrl+C
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_c);
		TS_ASSERT_EQUALS(ks.ascii, 0);
		TS_ASSERT_EQUALS(ks.flags, Common::KBD_CTRL);
		TS_ASSERT(dosBiosKeyState(0x2C1A, 0, 437, ks));    // Ctrl+Z, even if the BIOS flag is gone
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_z);
		TS_ASSERT_EQUALS(ks.flags, Common::KBD_CTRL);
	}

	void test_bios_buffer_extended_keys() {
		Common::KeyState ks;
		TS_ASSERT(dosBiosKeyState(0x4800, 0, 437, ks));    // keypad Up (no character)
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_UP);
		TS_ASSERT_EQUALS(ks.ascii, 0);
		TS_ASSERT(dosBiosKeyState(0x48E0, 0, 437, ks));    // gray Up: 0xE0 as the character
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_UP);
		TS_ASSERT(dosBiosKeyState(0x8600, 0, 437, ks));
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_F12);
	}

	void test_bios_buffer_alt_letters() {
		Common::KeyState ks;
		TS_ASSERT(dosBiosKeyState(0x1000, 0x08, 437, ks)); // Alt+Q
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_q);
		TS_ASSERT_EQUALS(ks.ascii, 0);
		TS_ASSERT_EQUALS(ks.flags, Common::KBD_ALT);
		TS_ASSERT(dosBiosKeyState(0x3200, 0, 437, ks));    // Alt+M: the Alt flag comes with the code
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_m);
		TS_ASSERT_EQUALS(ks.flags, Common::KBD_ALT);
	}

	void test_bios_buffer_characters_of_the_code_page() {
		Common::KeyState ks;
		TS_ASSERT(dosBiosKeyState(0x2882, 0, 850, ks));    // e acute on a French keyboard
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_INVALID);
		TS_ASSERT_EQUALS(ks.ascii, 0x00E9u);
		TS_ASSERT(dosBiosKeyState(0x1EA0, 0, 866, ks));    // Cyrillic a
		TS_ASSERT_EQUALS(ks.ascii, 0x0430u);
		TS_ASSERT(dosBiosKeyState(0x10E0, 0, 437, ks));    // 0xE0 under a gray key's scan code is alpha
		TS_ASSERT_EQUALS(ks.ascii, 0x03B1u);
	}

	void test_bios_buffer_keys_without_a_name_are_dropped() {
		Common::KeyState ks(Common::KEYCODE_x, 'x', 0);
		TS_ASSERT(!dosBiosKeyState(0x5400, 0, 437, ks));   // no name, types nothing
		TS_ASSERT(!dosBiosKeyState(0x0E7F, 0, 437, ks));   // Ctrl+Backspace (DEL)
		TS_ASSERT_EQUALS(ks.keycode, Common::KEYCODE_x);  // left alone
		TS_ASSERT_EQUALS(ks.ascii, 'x');
	}

private:
	DosScanState _state;
};
