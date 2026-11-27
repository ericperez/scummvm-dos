#include <cxxtest/TestSuite.h>

#include "common/config-manager.h"
#include "common/stream.h"
#include "test/system/null_osystem.h"
#include "test/system/temp_directory.h"

#include "backends/platform/dos/dos-filesys.h"

/**
 * The DOS port's file lookups, on a folder of the build machine: a file DOS lists under its 8.3 name is
 * found by the long name an engine asks for, and savegames whose names do not fit 8.3 are found and written
 * with or without a long file name driver (and found again by a system of the other kind). See dos-filesys.h.
 */
class DosFileSysTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
		#if NULL_OSYSTEM_IS_AVAILABLE && TEMP_DIRECTORY_IS_AVAILABLE
		Common::install_null_g_system();
		_dir = Common::make_temp_directory();
		ConfMan.setPath("savepath", Common::Path(_dir, '/'), Common::ConfigManager::kApplicationDomain);
		#endif
	}

	void tearDown() {
		#if NULL_OSYSTEM_IS_AVAILABLE && TEMP_DIRECTORY_IS_AVAILABLE
		ConfMan.removeKey("savepath", Common::ConfigManager::kApplicationDomain);
		Common::remove_temp_directory(_dir);
		Common::uninstall_null_g_system();
		#endif
	}

	void test_names_that_need_a_short_form() {
		TS_ASSERT(dosNeedsShortName("translations.dat"));
		TS_ASSERT(dosNeedsShortName("bladerunner-win.000"));
		TS_ASSERT(dosNeedsShortName("file.html"));         // the extension is too long
		TS_ASSERT(dosNeedsShortName("noextension"));
		TS_ASSERT(!dosNeedsShortName("kyra.dat"));
		TS_ASSERT(!dosNeedsShortName("12345678.123"));
		TS_ASSERT(!dosNeedsShortName("short"));
		TS_ASSERT(!dosNeedsShortName(""));
	}

	void test_a_long_name_finds_the_8_3_file() {
		if (_dir.empty())
			return;
		write("translat.dat", "DATA");
		DOSShortNameArchive archive{Common::FSNode(Common::Path(_dir, '/'))};

		TS_ASSERT(archive.hasFile("translations.dat"));
		Common::SeekableReadStream *in = archive.createReadStreamForMember("translations.dat");
		TS_ASSERT(in != nullptr);
		if (in)
			TS_ASSERT_EQUALS(in->size(), 4);
		delete in;
	}

	void test_a_file_that_is_not_there_is_not_found() {
		if (_dir.empty())
			return;
		DOSShortNameArchive archive{Common::FSNode(Common::Path(_dir, '/'))};

		TS_ASSERT(!archive.hasFile("translations.dat"));
		TS_ASSERT(archive.createReadStreamForMember("translations.dat") == nullptr);
	}

	void test_names_that_fit_83_are_left_to_the_folder() {
		if (_dir.empty())
			return;
		write("kyra.dat", "DATA");
		DOSShortNameArchive archive{Common::FSNode(Common::Path(_dir, '/'))};

		TS_ASSERT(!archive.hasFile("kyra.dat"));
	}

	void test_names_with_two_dots_or_a_leading_dot_are_not_cut() {
		if (_dir.empty())
			return;
		// foo.exe.rsrc would give foo.exe, ._foo.exe would give ._foo: not the same files
		write("foo.exe", "DATA");
		write("._foo.dat", "DATA");
		DOSShortNameArchive archive{Common::FSNode(Common::Path(_dir, '/'))};

		TS_ASSERT(!archive.hasFile("foo.exe.rsrc"));
		TS_ASSERT(!archive.hasFile("._foo.data"));
	}

	void test_a_name_can_be_found_but_not_listed() {
		if (_dir.empty())
			return;
		write("translat.dat", "DATA");
		DOSShortNameArchive archive{Common::FSNode(Common::Path(_dir, '/'))};
		Common::ArchiveMemberList list;

		TS_ASSERT_EQUALS(archive.listMatchingMembers(list, "translations.dat"), 1);
		TS_ASSERT_EQUALS(archive.listMatchingMembers(list, "trans*.dat"), 0);
		TS_ASSERT_EQUALS(archive.listMembers(list), 0);
	}

	// The save tests need the save manager, which the runner links only without cloud support
	void test_saves_with_names_that_fit_83_are_not_renamed() {
		#ifndef USE_CLOUD
		if (_dir.empty())
			return;
		DOSSaveFileManager saves(withLongNames);

		write(saves, "t7g.001");
		TS_ASSERT(exists("t7g.001"));
		TS_ASSERT(saves.exists("t7g.001"));
		#endif
	}

	void test_saves_keep_their_long_names_with_a_long_file_name_driver() {
		#ifndef USE_CLOUD
		if (_dir.empty())
			return;
		DOSSaveFileManager saves(withLongNames);

		write(saves, "bladerunner-win.001");
		TS_ASSERT(exists("bladerunner-win.001"));
		TS_ASSERT(saves.exists("bladerunner-win.001"));
		#endif
	}

	void test_saves_get_a_short_name_without_a_long_file_name_driver() {
		#ifndef USE_CLOUD
		if (_dir.empty())
			return;
		DOSSaveFileManager saves(withoutLongNames);
		Common::Array<Common::String> aliases;
		dosSaveAliases("bladerunner-win.001", aliases);

		write(saves, "bladerunner-win.001");
		TS_ASSERT(!exists("bladerunner-win.001"));
		TS_ASSERT(exists(aliases[0]));
		TS_ASSERT(saves.exists("bladerunner-win.001"));
		#endif
	}

	void test_two_targets_do_not_share_a_short_name() {
		#ifndef USE_CLOUD
		if (_dir.empty())
			return;
		DOSSaveFileManager saves(withoutLongNames);

		write(saves, "bladerunner-win.001");
		write(saves, "bladerunner-dos.001");
		Common::Array<Common::String> win, dos;
		dosSaveAliases("bladerunner-win.001", win);
		dosSaveAliases("bladerunner-dos.001", dos);

		TS_ASSERT(exists(win[0]));
		TS_ASSERT(exists(dos[0]));
		TS_ASSERT(win[0] != dos[0]);
		#endif
	}

	void test_a_pattern_with_a_fixed_base_lists_the_saves_by_their_long_names() {
		#ifndef USE_CLOUD
		if (_dir.empty())
			return;
		DOSSaveFileManager saves(withoutLongNames);

		write(saves, "bladerunner-win.001");
		write(saves, "bladerunner-win.002");
		write(saves, "bladerunner-dos.001");
		Common::StringArray found = saves.listSavefiles("bladerunner-win.###");
		Common::sort(found.begin(), found.end());

		TS_ASSERT_EQUALS(found.size(), 2u);
		if (found.size() == 2) {
			TS_ASSERT_EQUALS(found[0], "bladerunner-win.001");
			TS_ASSERT_EQUALS(found[1], "bladerunner-win.002");
		}
		#endif
	}

	void test_saves_made_with_a_long_file_name_driver_are_found_without_one() {
		#ifndef USE_CLOUD
		if (_dir.empty())
			return;
		// Windows and LFN drivers give blader~1.000 to the long name; the folder shows that
		write("blader~1.000", "SAVE");
		write("bladerun.001", "SAVE");   // and DJGPP without a driver cuts the name to 8
		DOSSaveFileManager saves(withoutLongNames);

		TS_ASSERT(saves.exists("bladerunner-win.000"));
		TS_ASSERT(saves.exists("bladerunner-win.001"));
		Common::InSaveFile *in = saves.openForLoading("bladerunner-win.000");
		TS_ASSERT(in != nullptr);
		delete in;
		TS_ASSERT_EQUALS(saves.listSavefiles("bladerunner-win.###").size(), 2u);
		#endif
	}

	void test_saves_made_without_a_driver_are_found_with_one() {
		#ifndef USE_CLOUD
		if (_dir.empty())
			return;
		DOSSaveFileManager without(withoutLongNames);
		write(without, "bladerunner-win.002");

		DOSSaveFileManager with(withLongNames);
		TS_ASSERT(with.exists("bladerunner-win.002"));
		Common::InSaveFile *in = with.openForLoading("bladerunner-win.002");
		TS_ASSERT(in != nullptr);
		delete in;
		#endif
	}

	void test_a_save_is_removed_by_its_long_name() {
		#ifndef USE_CLOUD
		if (_dir.empty())
			return;
		DOSSaveFileManager saves(withoutLongNames);
		write(saves, "bladerunner-win.003");

		TS_ASSERT(saves.removeSavefile("bladerunner-win.003"));
		TS_ASSERT(!saves.exists("bladerunner-win.003"));
		#endif
	}

	void test_saves_already_in_the_current_folder_are_listed() {
		// The DOS port has no default save path: an empty one is the current folder. The first
		// look at it has to list the folder, although "nothing cached yet" is an empty path too
		#ifndef USE_CLOUD
		if (_dir.empty())
			return;
		write("t7g.001", "SAVE"); // made by an earlier run
		write("t7g.002", "SAVE");

		const Common::String before = Common::change_current_directory(_dir);
		TS_ASSERT(!before.empty());
		if (before.empty())
			return;
		ConfMan.setPath("savepath", Common::Path(), Common::ConfigManager::kApplicationDomain);

		DOSSaveFileManager saves(withoutLongNames);
		Common::StringArray found = saves.listSavefiles("t7g.###");
		const bool exists = saves.exists("t7g.002");
		Common::change_current_directory(before);

		TS_ASSERT_EQUALS(found.size(), 2u);
		TS_ASSERT(exists);
		#endif
	}

private:
	Common::String _dir;

	static bool withLongNames() {
		return true;
	}
	static bool withoutLongNames() {
		return false;
	}

	/** Put a plain file in the folder, the way the system lists it */
	void write(const char *name, const char *text) {
		Common::FSNode node(Common::Path(_dir + "/" + name, '/'));
		Common::SeekableWriteStream *out = node.createWriteStream();
		TS_ASSERT(out != nullptr);
		if (!out)
			return;
		out->writeString(text);
		out->finalize();
		delete out;
	}

	#ifndef USE_CLOUD
	void write(DOSSaveFileManager &saves, const char *name) {
		Common::OutSaveFile *out = saves.openForSaving(name, false);
		TS_ASSERT(out != nullptr);
		if (!out)
			return;
		out->writeString("SAVE");
		out->finalize();
		delete out;
	}
	#endif

	bool exists(const Common::String &name) {
		return Common::FSNode(Common::Path(_dir + "/" + name, '/')).exists();
	}
};
