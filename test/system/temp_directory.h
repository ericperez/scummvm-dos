#ifndef TEST_TEMP_DIRECTORY
#define TEST_TEMP_DIRECTORY 1

#include "common/str.h"

namespace Common {

#ifdef POSIX
	/** Make an empty folder for a test to work in and return its path (without a trailing slash). */
	String make_temp_directory();

	/** Remove a folder made by make_temp_directory() and the plain files in it. */
	void remove_temp_directory(const String &path);

	/** Make a folder the current one, and return the path of the one that was (to go back to it). */
	String change_current_directory(const String &path);

	#define TEMP_DIRECTORY_IS_AVAILABLE 1
#else
	#define TEMP_DIRECTORY_IS_AVAILABLE 0
#endif

}

#endif
