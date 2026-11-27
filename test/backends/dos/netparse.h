#include <cxxtest/TestSuite.h>

#include <string.h>

#include "common/endian.h"
#include "common/str.h"

#include "backends/platform/dos/dos-netparse.h"

/**
 * The text handling of the DOS port's network layer: dotted addresses in and out, in network byte
 * order as SDL_net holds them (the first number is the first byte in memory). See dos-netparse.h.
 */
class DosNetParseTestSuite : public CxxTest::TestSuite {
public:
	void test_a_dotted_address() {
		uint32 host = 0;
		TS_ASSERT(dosParseDotted("192.168.1.5", host));
		TS_ASSERT_EQUALS(host, TO_BE_32(0xC0A80105u));           // 192.168.1.5 in network order
	}

	void test_the_extremes() {
		uint32 host = 1;
		TS_ASSERT(dosParseDotted("0.0.0.0", host));
		TS_ASSERT_EQUALS(host, 0u);
		TS_ASSERT(dosParseDotted("255.255.255.255", host));
		TS_ASSERT_EQUALS(host, 0xFFFFFFFFu);
		TS_ASSERT(dosParseDotted("127.0.0.1", host));
		TS_ASSERT_EQUALS(host, TO_BE_32(0x7F000001u));
	}

	void test_each_number_goes_in_its_own_byte() {
		uint32 host = 0;
		TS_ASSERT(dosParseDotted("1.2.3.4", host));
		const byte *bytes = (const byte *)&host;               // in memory, in the order written
		TS_ASSERT_EQUALS(bytes[0], 1);
		TS_ASSERT_EQUALS(bytes[1], 2);
		TS_ASSERT_EQUALS(bytes[2], 3);
		TS_ASSERT_EQUALS(bytes[3], 4);
		TS_ASSERT_EQUALS(READ_BE_UINT32(&host), 0x01020304u);
	}

	void test_numbers_over_255_are_refused() {
		uint32 host = 0;
		TS_ASSERT(!dosParseDotted("256.1.1.1", host));
		TS_ASSERT(!dosParseDotted("1.1.1.256", host));
		TS_ASSERT(!dosParseDotted("999.1.1.1", host));
		TS_ASSERT(!dosParseDotted("300.0.0.0", host));
		TS_ASSERT(!dosParseDotted("1.2.3.99999999999", host));   // does not overflow its way into range
	}

	void test_text_that_is_not_an_address_is_refused() {
		uint32 host = 0;
		TS_ASSERT(!dosParseDotted("", host));
		TS_ASSERT(!dosParseDotted("localhost", host));
		TS_ASSERT(!dosParseDotted("www.example.com", host));
		TS_ASSERT(!dosParseDotted("a.b.c.d", host));
		TS_ASSERT(!dosParseDotted("1.2.3", host));               // three numbers
		TS_ASSERT(!dosParseDotted("127.1", host));               // the shortened forms of old resolvers
		TS_ASSERT(!dosParseDotted("1.2.3.4.5", host));           // five
		TS_ASSERT(!dosParseDotted("1.2.3.", host));
		TS_ASSERT(!dosParseDotted("1..2.3", host));
		TS_ASSERT(!dosParseDotted(".1.2.3", host));
		TS_ASSERT(!dosParseDotted("-1.2.3.4", host));
		TS_ASSERT(!dosParseDotted("+1.2.3.4", host));
		TS_ASSERT(!dosParseDotted("1.2.3.4x", host));
		TS_ASSERT(!dosParseDotted("1.2.3.4 ", host));
		TS_ASSERT(!dosParseDotted(" 1.2.3.4", host));
		TS_ASSERT(!dosParseDotted("1,2,3,4", host));
		TS_ASSERT(!dosParseDotted("0x7f.0.0.1", host));
	}

	void test_a_refused_address_leaves_the_result_alone() {
		uint32 host = 0xDEADBEEF;
		TS_ASSERT(!dosParseDotted("1.2.3", host));
		TS_ASSERT_EQUALS(host, 0xDEADBEEFu);
		TS_ASSERT(!dosParseDotted("300.1.1.1", host));
		TS_ASSERT_EQUALS(host, 0xDEADBEEFu);
	}

	void test_formatting_an_address_in_sdl_net_order() {
		char text[16];
		dosFormatHost(text, sizeof(text), TO_BE_32(0xC0A80105u));
		TS_ASSERT_EQUALS(Common::String(text), "192.168.1.5");
		dosFormatHost(text, sizeof(text), 0);
		TS_ASSERT_EQUALS(Common::String(text), "0.0.0.0");
		dosFormatHost(text, sizeof(text), 0xFFFFFFFFu);
		TS_ASSERT_EQUALS(Common::String(text), "255.255.255.255");  // 15 characters: fits 16 bytes
	}

	void test_formatting_numbers() {
		char text[16];
		dosFormatDotted(text, sizeof(text), 10, 0, 2, 15);
		TS_ASSERT_EQUALS(Common::String(text), "10.0.2.15");
	}

	void test_formatting_never_writes_past_the_buffer() {
		char text[8];
		memset(text, 'x', sizeof(text));
		dosFormatDotted(text, 6, 255, 255, 255, 255);            // too long for 6 bytes: cut, and ended
		TS_ASSERT_EQUALS(text[5], 0);
		TS_ASSERT_EQUALS(text[6], 'x');
		TS_ASSERT_EQUALS(text[7], 'x');
	}

	void test_parsing_and_formatting_agree() {
		const char *addresses[] = { "10.0.2.15", "192.168.0.1", "172.16.254.3", "255.0.255.0", "8.8.8.8", "1.0.0.0", "0.0.0.1" };
		for (unsigned i = 0; i < ARRAYSIZE(addresses); i++) {
			uint32 host = 0;
			char text[16];
			TS_ASSERT(dosParseDotted(addresses[i], host));
			dosFormatHost(text, sizeof(text), host);
			TS_ASSERT_EQUALS(Common::String(text), addresses[i]);
		}
	}

	void test_formatting_an_address_written_by_the_network_stack() {
		const byte fromStack[4] = { 10, 0, 2, 15 };                 // as gethostbyname() copies it
		uint32 host;
		memcpy(&host, fromStack, sizeof(host));
		char text[16];
		dosFormatHost(text, sizeof(text), host);
		TS_ASSERT_EQUALS(Common::String(text), "10.0.2.15");
	}

	void test_a_port_number_in_network_order() {
		const uint16 port = TO_BE_16(12345);                        // the web server's default port
		const byte *bytes = (const byte *)&port;
		TS_ASSERT_EQUALS(bytes[0], 0x30);                           // the high byte first in memory
		TS_ASSERT_EQUALS(bytes[1], 0x39);
	}
};
