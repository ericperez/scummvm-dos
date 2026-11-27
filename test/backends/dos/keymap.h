#include <cxxtest/TestSuite.h>

#include "backends/platform/dos/dos-keymap.h"

/**
 * The keyboard layout logic of the DOS port: what a key press types and is called, from the
 * character the BIOS (or KEYB) typed for it and the DOS code page. See dos-keymap.h and dos-bioskey.h.
 */
class DosKeymapTestSuite : public CxxTest::TestSuite {
public:
	void test_ascii_is_itself_in_every_code_page() {
		for (unsigned i = 0; i < kDosCodePageCount; i++) {
			for (unsigned ch = 0; ch < 0x80; ch++)
				TS_ASSERT_EQUALS(dosCharToUnicode(kDosCodePageNumbers[i], (uint8)ch), ch);
		}
	}

	void test_code_page_437() {
		TS_ASSERT_EQUALS(dosCharToUnicode(437, 0x82), 0x00E9u); // e acute
		TS_ASSERT_EQUALS(dosCharToUnicode(437, 0x81), 0x00FCu); // u diaeresis
		TS_ASSERT_EQUALS(dosCharToUnicode(437, 0xE1), 0x00DFu); // sharp s
		TS_ASSERT_EQUALS(dosCharToUnicode(437, 0xA4), 0x00F1u); // n tilde
		TS_ASSERT_EQUALS(dosCharToUnicode(437, 0xDB), 0x2588u); // full block
	}

	void test_code_page_850_and_858() {
		TS_ASSERT_EQUALS(dosCharToUnicode(850, 0x9B), 0x00F8u); // o stroke: not in 437
		TS_ASSERT_EQUALS(dosCharToUnicode(850, 0xD5), 0x0131u); // dotless i
		TS_ASSERT_EQUALS(dosCharToUnicode(858, 0xD5), 0x20ACu); // 858 is 850 with the euro sign there
		TS_ASSERT_EQUALS(dosCharToUnicode(858, 0x9B), 0x00F8u);
	}

	void test_other_code_pages() {
		TS_ASSERT_EQUALS(dosCharToUnicode(852, 0xA4), 0x0104u); // A ogonek (Central Europe)
		TS_ASSERT_EQUALS(dosCharToUnicode(866, 0x80), 0x0410u); // Cyrillic A
		TS_ASSERT_EQUALS(dosCharToUnicode(737, 0x80), 0x0391u); // Greek Alpha
		TS_ASSERT_EQUALS(dosCharToUnicode(862, 0x80), 0x05D0u); // Hebrew Alef
		TS_ASSERT_EQUALS(dosCharToUnicode(863, 0x8E), 0x00C0u); // A grave (Canadian French)
		TS_ASSERT_EQUALS(dosCharToUnicode(865, 0x9B), 0x00F8u); // Nordic
	}

	void test_unknown_code_page_reads_as_437() {
		TS_ASSERT_EQUALS(dosCharToUnicode(9999, 0x82), 0x00E9u);
		TS_ASSERT_EQUALS(dosCharToUnicode(0, 0x82), 0x00E9u);
	}

	void test_keycode_for_char() {
		TS_ASSERT_EQUALS(dosKeycodeForChar('a'), Common::KEYCODE_a);
		TS_ASSERT_EQUALS(dosKeycodeForChar('A'), Common::KEYCODE_a); // a key has no case
		TS_ASSERT_EQUALS(dosKeycodeForChar('7'), Common::KEYCODE_7);
		TS_ASSERT_EQUALS(dosKeycodeForChar(' '), Common::KEYCODE_SPACE);
		TS_ASSERT_EQUALS(dosKeycodeForChar('@'), Common::KEYCODE_AT);
		TS_ASSERT_EQUALS(dosKeycodeForChar(0x00E9), Common::KEYCODE_INVALID); // not ASCII
		TS_ASSERT_EQUALS(dosKeycodeForChar(13), Common::KEYCODE_INVALID);     // a control character
	}

	void test_bios_word_matches_by_scan_code() {
		TS_ASSERT(dosBiosWordMatches(0x1E61, 0x1E, false));  // 'a' on the A key
		TS_ASSERT(!dosBiosWordMatches(0x1E61, 0x30, false)); // a later press's word
		TS_ASSERT(!dosBiosWordMatches(0x0000, 0x1E, false)); // empty buffer
		TS_ASSERT(dosBiosWordMatches(0x4800 | 0xE0, 0x48, true)); // gray arrow: scan in the high byte
		// Keypad Enter and keypad / are queued with 0xE0 as the scan code
		TS_ASSERT(dosBiosWordMatches(0xE00D, 0x1C, true));
		TS_ASSERT(dosBiosWordMatches(0xE02F, 0x35, true));
		TS_ASSERT(!dosBiosWordMatches(0x1C0D, 0x1C, true));
		TS_ASSERT(dosBiosWordMatches(0x1C0D, 0x1C, false));   // the main Enter key is a plain one
	}

	void test_text_control_characters() {
		TS_ASSERT(dosIsTextControl(0x0E, false, 8));    // Backspace
		TS_ASSERT(dosIsTextControl(0x0F, false, 9));    // Tab
		TS_ASSERT(dosIsTextControl(0x1C, false, 13));   // Enter
		TS_ASSERT(dosIsTextControl(0x01, false, 27));   // Escape
		TS_ASSERT(dosIsTextControl(0x1C, true, 13));    // keypad Enter
		TS_ASSERT(!dosIsTextControl(0x23, false, 8));   // Ctrl+H is no Backspace
		TS_ASSERT(!dosIsTextControl(0x2E, false, 3));   // Ctrl+C
		TS_ASSERT(!dosIsTextControl(0x1C, false, 10));  // Ctrl+Enter
	}

	void test_us_letter() {
		DosTypedKey k = dosTypedKey(437, 'a', 0x1E, false, Common::KEYCODE_a, false, false);
		TS_ASSERT_EQUALS(k.unicode, 'a');
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_a);
		TS_ASSERT(!k.dropAlt);
	}

	void test_german_y_and_z_swap() {
		// The key in the US 'y' place (scan 0x15) types 'z' on a German keyboard, and is named z
		DosTypedKey k = dosTypedKey(850, 'z', 0x15, false, Common::KEYCODE_y, false, false);
		TS_ASSERT_EQUALS(k.unicode, 'z');
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_z);
	}

	void test_french_azerty() {
		// The US 'q' place types 'a', the US 'a' place types 'q', 'w' and 'z' swap
		DosTypedKey k = dosTypedKey(850, 'a', 0x10, false, Common::KEYCODE_q, false, false);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_a);
		k = dosTypedKey(850, 'q', 0x1E, false, Common::KEYCODE_a, false, false);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_q);
		k = dosTypedKey(850, 'W', 0x2C, false, Common::KEYCODE_z, true, false);
		TS_ASSERT_EQUALS(k.unicode, 'W');
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_w);
	}

	void test_accented_letters_are_unicode_without_a_key_name() {
		DosTypedKey k = dosTypedKey(850, 0x82, 0x28, false, Common::KEYCODE_QUOTE, false, false); // e acute
		TS_ASSERT_EQUALS(k.unicode, 0x00E9u);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_QUOTE); // no ASCII name for the character: the key's place
		k = dosTypedKey(866, 0xA0, 0x1E, false, Common::KEYCODE_a, false, false); // Cyrillic a
		TS_ASSERT_EQUALS(k.unicode, 0x0430u);
	}

	void test_shifted_symbols_keep_the_key_name() {
		// Shift+1 types '!' and the key is still the 1 key, as under SDL
		DosTypedKey k = dosTypedKey(437, '!', 0x02, false, Common::KEYCODE_1, true, false);
		TS_ASSERT_EQUALS(k.unicode, '!');
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_1);

		// On an AZERTY keyboard the digits need Shift: Shift+the '&' key is '1' and is named 1
		k = dosTypedKey(850, '1', 0x02, false, Common::KEYCODE_1, true, false);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_1);

		// Unshifted, the same key types '&' and is named by it
		k = dosTypedKey(850, '&', 0x02, false, Common::KEYCODE_1, false, false);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_AMPERSAND);
	}

	void test_altgr_makes_text_without_alt() {
		DosTypedKey k = dosTypedKey(850, '@', 0x10, false, Common::KEYCODE_q, false, true);
		TS_ASSERT_EQUALS(k.unicode, '@');
		TS_ASSERT(k.dropAlt);

		// The left Alt is a shortcut key and stays one
		k = dosTypedKey(850, '@', 0x10, false, Common::KEYCODE_q, false, false);
		TS_ASSERT(!k.dropAlt);

		// AltGr with a key that types nothing is no text
		k = dosTypedKey(850, 0, 0x10, false, Common::KEYCODE_q, false, true);
		TS_ASSERT_EQUALS(k.unicode, 0u);
		TS_ASSERT(!k.dropAlt);
	}

	void test_dead_key_types_nothing_but_is_still_a_key() {
		DosTypedKey k = dosTypedKey(850, 0, 0x1B, false, Common::KEYCODE_RIGHTBRACKET, false, false);
		TS_ASSERT_EQUALS(k.unicode, 0u);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_RIGHTBRACKET);
	}

	void test_control_characters_are_no_text_except_the_text_keys() {
		DosTypedKey k = dosTypedKey(437, 3, 0x2E, false, Common::KEYCODE_c, false, false); // Ctrl+C
		TS_ASSERT_EQUALS(k.unicode, 0u);
		k = dosTypedKey(437, 8, 0x0E, false, Common::KEYCODE_BACKSPACE, false, false);
		TS_ASSERT_EQUALS(k.unicode, 8u);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_BACKSPACE);
		k = dosTypedKey(437, 13, 0x1C, false, Common::KEYCODE_RETURN, false, false);
		TS_ASSERT_EQUALS(k.unicode, 13u);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_RETURN);
	}

	void test_gray_navigation_keys_type_nothing() {
		// The BIOS puts 0xE0 where the character goes for a gray key: that is not a character
		DosTypedKey k = dosTypedKey(437, 0xE0, 0x48, true, Common::KEYCODE_UP, false, false);
		TS_ASSERT_EQUALS(k.unicode, 0u);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_UP);
		k = dosTypedKey(437, 0xE0, 0x53, true, Common::KEYCODE_DELETE, false, false);
		TS_ASSERT_EQUALS(k.unicode, 0u);
	}

	void test_function_keys_type_nothing() {
		DosTypedKey k = dosTypedKey(437, 0, 0x3B, false, Common::KEYCODE_F1, false, false);
		TS_ASSERT_EQUALS(k.unicode, 0u);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_F1);
		// Even if a buffer word carried a character
		k = dosTypedKey(437, 'x', 0x3B, false, Common::KEYCODE_F1, false, false);
		TS_ASSERT_EQUALS(k.unicode, 0u);
	}

	void test_keypad_keeps_its_names() {
		// Num Lock on: the keypad types digits but the key is still the keypad's
		DosTypedKey k = dosTypedKey(437, '7', 0x47, false, Common::KEYCODE_KP7, false, false);
		TS_ASSERT_EQUALS(k.unicode, '7');
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_KP7);
		k = dosTypedKey(437, '*', 0x37, false, Common::KEYCODE_KP_MULTIPLY, false, false);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_KP_MULTIPLY);
		// Keypad Enter and keypad / are gray keys that do type
		k = dosTypedKey(437, 13, 0x1C, true, Common::KEYCODE_KP_ENTER, false, false);
		TS_ASSERT_EQUALS(k.unicode, 13u);
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_KP_ENTER);
		k = dosTypedKey(437, '/', 0x35, true, Common::KEYCODE_KP_DIVIDE, false, false);
		TS_ASSERT_EQUALS(k.unicode, '/');
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_KP_DIVIDE);
	}

	void test_extra_iso_key_types_text_even_without_a_us_name() {
		// The key between Left Shift and Z (scan 0x56) types '<' on a German keyboard; it has no US name
		DosTypedKey k = dosTypedKey(850, '<', 0x56, false, Common::KEYCODE_INVALID, false, false);
		TS_ASSERT_EQUALS(k.unicode, '<');
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_LESS);
	}

	void test_keys_that_type_nothing() {
		// Function keys F1 to F10, F11 and F12: the BIOS queues them with no character
		for (uint8 scan = 0x3B; scan <= 0x44; scan++)
			TS_ASSERT(dosKeyTypesNothing(scan, false));
		TS_ASSERT(dosKeyTypesNothing(0x57, false));
		TS_ASSERT(dosKeyTypesNothing(0x58, false));
		// The gray keys (arrows, Insert, Home, ...), all but keypad Enter and keypad /
		TS_ASSERT(dosKeyTypesNothing(0x48, true));
		TS_ASSERT(dosKeyTypesNothing(0x53, true));
		TS_ASSERT(dosKeyTypesNothing(0x1D, true));  // right Ctrl
		TS_ASSERT(!dosKeyTypesNothing(0x1C, true));
		TS_ASSERT(!dosKeyTypesNothing(0x35, true));
	}

	void test_keys_that_may_type() {
		TS_ASSERT(!dosKeyTypesNothing(0x1E, false)); // A
		TS_ASSERT(!dosKeyTypesNothing(0x39, false)); // Space
		TS_ASSERT(!dosKeyTypesNothing(0x1C, false)); // Enter
		TS_ASSERT(!dosKeyTypesNothing(0x48, false)); // keypad 8, with Num Lock on
		TS_ASSERT(!dosKeyTypesNothing(0x3A, false)); // the keys just before and after the function keys
		TS_ASSERT(!dosKeyTypesNothing(0x45, false));
		TS_ASSERT(!dosKeyTypesNothing(0x56, false)); // the extra ISO key
	}
};
