#include <cxxtest/TestSuite.h>

#include "backends/platform/dos/dos-names.h"

/**
 * Names on the DOS port: the 8.3 name DOS gives a file (without a long file name driver an engine's
 * translations.dat is TRANSLAT.DAT, and the program has to look for that), and the language that goes
 * with a CONFIG.SYS country. See dos-names.h.
 */
class DosNamesTestSuite : public CxxTest::TestSuite {
public:
	void test_short_names_are_cut_to_8_and_3() {
		TS_ASSERT_EQUALS(dosShortName("translations.dat"), "translat.dat");
		TS_ASSERT_EQUALS(dosShortName("classicmacfonts.dat"), "classicm.dat");
		TS_ASSERT_EQUALS(dosShortName("helpdialog.zip"), "helpdial.zip");
		TS_ASSERT_EQUALS(dosShortName("file.html"), "file.htm");
	}

	void test_names_that_fit_are_left_alone() {
		TS_ASSERT_EQUALS(dosShortName("kyra.dat"), "kyra.dat");
		TS_ASSERT_EQUALS(dosShortName("wwwroot.zip"), "wwwroot.zip");
		TS_ASSERT_EQUALS(dosShortName("queen.tbl"), "queen.tbl");
		TS_ASSERT_EQUALS(dosShortName("12345678.123"), "12345678.123");
		TS_ASSERT_EQUALS(dosShortName("noextension"), "noextens");
		TS_ASSERT_EQUALS(dosShortName("short"), "short");
	}

	void test_only_the_first_dot_counts() {
		TS_ASSERT_EQUALS(dosShortName("a.b.c"), "a.b");          // the extension ends at the next dot
		TS_ASSERT_EQUALS(dosShortName("archive.tar.gz"), "archive.tar");
		TS_ASSERT_EQUALS(dosShortName("my.file.name.txt"), "my.fil");   // "my", then "fil" of "file"
	}

	void test_save_names_that_fit_83() {
		TS_ASSERT(dosNameFits83("t7g.001"));
		TS_ASSERT(dosNameFits83("bladerun.000"));
		TS_ASSERT(!dosNameFits83("bladerunner.000"));
		TS_ASSERT(!dosNameFits83("bladerunner-win.000"));
		TS_ASSERT(!dosNameFits83("a.b.c"));
		TS_ASSERT(!dosNameFits83("name.long"));
	}

	void test_save_aliases() {
		Common::Array<Common::String> a;
		dosSaveAliases("bladerunner-win.000", a);
		TS_ASSERT_EQUALS(a.size(), 11u);
		TS_ASSERT_EQUALS(a[1], "blader~1.000");
		TS_ASSERT_EQUALS(a[9], "blader~9.000");
		TS_ASSERT_EQUALS(a[10], "bladerun.000");
		TS_ASSERT_EQUALS(a[0].size(), 12u);
		TS_ASSERT(a[0].hasPrefix("blade"));
		TS_ASSERT(a[0].hasSuffix(".000"));
		TS_ASSERT(dosNameFits83(a[0]));
	}

	void test_hash_alias_tells_targets_apart() {
		Common::Array<Common::String> a, b, c;
		dosSaveAliases("bladerunner-win.000", a);
		dosSaveAliases("bladerunner-dos.000", b);
		dosSaveAliases("BladeRunner-Win.000", c);
		TS_ASSERT(a[0] != b[0]);
		TS_ASSERT_EQUALS(a[0], c[0]);   // not case sensitive
	}

	void test_edge_cases() {
		TS_ASSERT_EQUALS(dosShortName(""), "");
		TS_ASSERT_EQUALS(dosShortName("name."), "name");         // an empty extension has no dot
		TS_ASSERT_EQUALS(dosShortName(".hidden"), ".hid");
		TS_ASSERT_EQUALS(dosShortName("a"), "a");
	}

	void test_the_result_always_fits_8_dot_3() {
		const char *names[] = { "translations.dat", "x", "abcdefghijklmnop.qrstuvwxyz", "a.b.c.d.e", "........", "longname.e" };
		for (unsigned i = 0; i < ARRAYSIZE(names); i++) {
			const Common::String s = dosShortName(names[i]);
			const uint dot = s.findFirstOf('.');
			if (dot == Common::String::npos) {
				TS_ASSERT(s.size() <= 8);
			} else {
				TS_ASSERT(dot <= 8);
				TS_ASSERT(s.size() - dot - 1 <= 3);
			}
		}
	}

	void test_countries_and_their_languages() {
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(49)), "de_DE");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(33)), "fr_FR");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(34)), "es_ES");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(39)), "it_IT");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(7)), "ru_RU");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(972)), "he_IL");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(44)), "en_GB");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(55)), "pt_BR");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(351)), "pt_PT");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(46)), "sv_SE");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(47)), "nb_NO");
	}

	void test_the_united_states_and_unknown_countries_are_english() {
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(1)), "en_US");     // the US code is 1
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(0)), "en_US");
		TS_ASSERT_EQUALS(Common::String(dosLanguageForCountry(9999)), "en_US");
	}

	void test_every_language_is_written_as_ScummVM_writes_them() {
		for (unsigned country = 0; country < 1000; country++) {
			const char *lang = dosLanguageForCountry(country);
			TS_ASSERT(lang != nullptr);
			if (!lang)
				continue;
			TS_ASSERT_EQUALS(strlen(lang), 5u);
			TS_ASSERT(lang[0] >= 'a' && lang[0] <= 'z');
			TS_ASSERT(lang[1] >= 'a' && lang[1] <= 'z');
			TS_ASSERT_EQUALS(lang[2], '_');
			TS_ASSERT(lang[3] >= 'A' && lang[3] <= 'Z');
			TS_ASSERT(lang[4] >= 'A' && lang[4] <= 'Z');
		}
	}

	void test_the_countries_with_their_own_language_are_covered() {
		// The countries the table knows about, none of them English by mistake
		const unsigned known[] = { 7, 20, 30, 31, 32, 33, 34, 36, 39, 41, 43, 45, 46, 47, 48, 49, 55, 90, 351, 358, 380, 420, 421, 966, 971, 972 };
		for (unsigned i = 0; i < ARRAYSIZE(known); i++)
			TS_ASSERT(Common::String(dosLanguageForCountry(known[i])) != "en_US");
	}
};
