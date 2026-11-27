#include <cxxtest/TestSuite.h>

#include "backends/platform/dos/dos-cd.h"

/**
 * The CD audio logic of the DOS port: Red Book addresses and HSG sectors, which frames a track (or
 * part of one) is, the requests sent to MSCDEX's device driver and the answers read from it, volume,
 * the bookkeeping of a track that plays more than once, and the sound card's CD input. See dos-cd.h.
 */
class DosCdTestSuite : public CxxTest::TestSuite {
	/** A disc of three tracks: 1 is 4 seconds from 00:02:00, 2 is 6 seconds, 3 is 3 seconds */
	static DosCdToc threeTracks() {
		DosCdToc toc;
		toc.first = 1;
		toc.last = 3;
		toc.start[1] = 150;
		toc.start[2] = 150 + 300;
		toc.start[3] = 150 + 300 + 450;
		toc.leadOut = 150 + 300 + 450 + 225;
		return toc;
	}

public:
	void test_red_book_addresses() {
		TS_ASSERT_EQUALS(dosCdFramesToRedBook(0), 0u);
		TS_ASSERT_EQUALS(dosCdFramesToRedBook(150), 0x00000200u);              // 00:02:00, where track 1 starts
		TS_ASSERT_EQUALS(dosCdFramesToRedBook(4580), 0x00010105u);             // 01:01:05
		TS_ASSERT_EQUALS(dosCdFramesToRedBook(74), 0x0000004Au);               // the last frame of a second
		TS_ASSERT_EQUALS(dosCdFramesToRedBook(4499), 0x00003B4Au);             // 00:59:74: seconds roll over to minutes after 59
		TS_ASSERT_EQUALS(dosCdFramesToRedBook(4500), 0x00010000u);             // 01:00:00
		TS_ASSERT_EQUALS(dosCdFramesToRedBook(75 * 60 * 74), 0x004A0000u);     // 74 minutes: a full disc
	}

	void test_red_book_round_trip() {
		for (uint32 f = 0; f < 75 * 60 * 80; f += 997)
			TS_ASSERT_EQUALS(dosCdRedBookToFrames(dosCdFramesToRedBook(f)), f);
		TS_ASSERT_EQUALS(dosCdRedBookToFrames(0x00010203u), 75u * 60 + 2 * 75 + 3);
	}

	void test_frames_to_milliseconds() {
		TS_ASSERT_EQUALS(dosCdFramesToMillis(75), 1000u);
		TS_ASSERT_EQUALS(dosCdFramesToMillis(0), 0u);
		TS_ASSERT_EQUALS(dosCdFramesToMillis(38), 506u);
		// A whole 74 minute disc must not overflow the arithmetic
		TS_ASSERT_EQUALS(dosCdFramesToMillis(75 * 60 * 74), 74u * 60 * 1000);
	}

	void test_track_lengths() {
		const DosCdToc toc = threeTracks();
		TS_ASSERT(toc.valid());
		TS_ASSERT_EQUALS(toc.length(1), 300u);
		TS_ASSERT_EQUALS(toc.length(2), 450u);
		TS_ASSERT_EQUALS(toc.length(3), 225u);      // the last runs to the end of the disc
		TS_ASSERT_EQUALS(toc.length(0), 0u);
		TS_ASSERT_EQUALS(toc.length(4), 0u);
	}

	void test_a_table_of_contents_needs_to_make_sense() {
		DosCdToc empty;
		TS_ASSERT(!empty.valid());

		DosCdToc backwards = threeTracks();
		backwards.leadOut = backwards.start[3];     // the disc ends where the last track starts
		TS_ASSERT(!backwards.valid());
	}

	void test_whole_track() {
		const DosCdToc toc = threeTracks();
		DosCdRange range;
		TS_ASSERT(dosCdTrackRange(toc, 2, 0, 0, range));
		TS_ASSERT_EQUALS(range.start, 450u);
		TS_ASSERT_EQUALS(range.frames, 450u);

		TS_ASSERT(dosCdTrackRange(toc, 3, 0, 0, range));
		TS_ASSERT_EQUALS(range.start, 900u);
		TS_ASSERT_EQUALS(range.frames, 225u);
	}

	void test_part_of_a_track() {
		const DosCdToc toc = threeTracks();
		DosCdRange range;
		TS_ASSERT(dosCdTrackRange(toc, 3, 75, 75, range));       // a second in, for a second
		TS_ASSERT_EQUALS(range.start, 900u + 75);
		TS_ASSERT_EQUALS(range.frames, 75u);

		TS_ASSERT(dosCdTrackRange(toc, 3, 75, 0, range));        // a second in, to the end
		TS_ASSERT_EQUALS(range.start, 900u + 75);
		TS_ASSERT_EQUALS(range.frames, 150u);
	}

	void test_a_duration_that_is_too_long_stops_at_the_end_of_the_track() {
		const DosCdToc toc = threeTracks();
		DosCdRange range;
		TS_ASSERT(dosCdTrackRange(toc, 1, 250, 100, range));     // track 1 has 50 frames left
		TS_ASSERT_EQUALS(range.start, 150u + 250);
		TS_ASSERT_EQUALS(range.frames, 50u);                     // not 100: it would play into track 2

		TS_ASSERT(dosCdTrackRange(toc, 2, 0, 100000, range));
		TS_ASSERT_EQUALS(range.frames, 450u);
	}

	/**
	 * ScummVM's track numbers are an index into the table of contents (the SDL, Windows and macOS CD
	 * managers do the same), so the first track of the disc is 0
	 */
	void test_scummvm_counts_tracks_from_zero() {
		const DosCdToc toc = threeTracks();
		TS_ASSERT_EQUALS(dosCdPhysicalTrack(toc, 0), 1);
		TS_ASSERT_EQUALS(dosCdPhysicalTrack(toc, 1), 2);
		TS_ASSERT_EQUALS(dosCdPhysicalTrack(toc, 2), 3);
		TS_ASSERT_EQUALS(dosCdPhysicalTrack(toc, 3), -1);        // past the last track
		TS_ASSERT_EQUALS(dosCdPhysicalTrack(toc, -1), -1);
		TS_ASSERT_EQUALS(dosCdPhysicalTrack(toc, 99), -1);
	}

	/**
	 * A game CD: a data track first, the music after it. The engines ask for the music as 1, 2, ...
	 * (King's Quest VI's "track 2" is asked for as 1), which are the disc's tracks 2, 3, ...
	 */
	void test_a_game_cd_with_a_data_track_first() {
		DosCdToc toc;
		toc.first = 1;
		toc.last = 4;
		toc.data[1] = true;
		toc.start[1] = 150;                                      // the data track: 100 seconds
		toc.start[2] = 150 + 7500;
		toc.start[3] = 150 + 7500 + 3000;
		toc.start[4] = 150 + 7500 + 3000 + 4500;
		toc.leadOut = 150 + 7500 + 3000 + 4500 + 6000;

		DosCdRange range;
		TS_ASSERT_EQUALS(dosCdPhysicalTrack(toc, 1), 2);
		TS_ASSERT(dosCdTrackRange(toc, dosCdPhysicalTrack(toc, 1), 0, 0, range));
		TS_ASSERT_EQUALS(range.start, 7650u);
		TS_ASSERT_EQUALS(range.frames, 3000u);

		TS_ASSERT(dosCdTrackRange(toc, dosCdPhysicalTrack(toc, 3), 0, 0, range));    // the last music track
		TS_ASSERT_EQUALS(range.frames, 6000u);

		// Track 0 is the data track: not playable
		TS_ASSERT(!dosCdTrackRange(toc, dosCdPhysicalTrack(toc, 0), 0, 0, range));
	}

	void test_refused_requests() {
		const DosCdToc toc = threeTracks();
		DosCdRange range;
		TS_ASSERT(!dosCdTrackRange(toc, 0, 0, 0, range));
		TS_ASSERT(!dosCdTrackRange(toc, 4, 0, 0, range));
		TS_ASSERT(!dosCdTrackRange(toc, -1, 0, 0, range));
		TS_ASSERT(!dosCdTrackRange(toc, 2, 450, 0, range));      // starts where the track ends
		TS_ASSERT(!dosCdTrackRange(toc, 2, 100000, 0, range));
		TS_ASSERT(!dosCdTrackRange(toc, 2, -1, 0, range));
		TS_ASSERT(!dosCdTrackRange(toc, 2, 0, -5, range));

		DosCdToc none;
		TS_ASSERT(!dosCdTrackRange(none, 1, 0, 0, range));       // no disc
	}

	void test_data_tracks_are_not_music() {
		DosCdToc toc = threeTracks();
		toc.data[1] = true;                                      // a mixed mode disc: data first, then music
		DosCdRange range;
		TS_ASSERT(!dosCdTrackRange(toc, 1, 0, 0, range));
		TS_ASSERT(dosCdTrackRange(toc, 2, 0, 0, range));
		TS_ASSERT_EQUALS(range.start, 450u);
	}

	void test_hsg_sectors() {
		TS_ASSERT_EQUALS(dosCdFramesToHsg(150), 0u);             // 00:02:00, where track 1 starts
		TS_ASSERT_EQUALS(dosCdFramesToHsg(151), 1u);
		TS_ASSERT_EQUALS(dosCdFramesToHsg(4580), 4430u);         // 01:01:05
		TS_ASSERT_EQUALS(dosCdFramesToHsg(0), 0u);               // inside the lead-in: its start
		TS_ASSERT_EQUALS(dosCdFramesToHsg(149), 0u);
	}

	void test_play_request_layout() {
		uint8 request[kDosCdPlayRequestSize];
		memset(request, 0xEE, sizeof(request));
		const int size = dosCdBuildPlay(request, 450, 450);

		TS_ASSERT_EQUALS(size, 22);
		TS_ASSERT_EQUALS(request[0], 22);                        // length of the request
		TS_ASSERT_EQUALS(request[1], 0);                         // subunit
		TS_ASSERT_EQUALS(request[2], 0x84);                      // command: play audio
		TS_ASSERT_EQUALS(dosCdGet16(request + 3), 0);            // status: the driver fills it in
		for (int i = 5; i < 13; i++)
			TS_ASSERT_EQUALS(request[i], 0);                     // reserved
		TS_ASSERT_EQUALS(request[13], 0);                        // HSG addressing, which every driver takes
		TS_ASSERT_EQUALS(dosCdGet32(request + 14), 300u);        // 00:06:00 is HSG sector 300
		TS_ASSERT_EQUALS(dosCdGet32(request + 18), 450u);        // number of frames
	}

	void test_play_request_start_is_a_little_endian_hsg_sector() {
		uint8 request[kDosCdPlayRequestSize];
		dosCdBuildPlay(request, 4580, 1);                        // 01:01:05: sector 4430 = 0x114E
		TS_ASSERT_EQUALS(request[14], 0x4E);
		TS_ASSERT_EQUALS(request[15], 0x11);
		TS_ASSERT_EQUALS(request[16], 0);
		TS_ASSERT_EQUALS(request[17], 0);

		dosCdBuildPlay(request, 75 * 60 * 74, 1);                // 74 minutes in: more than 16 bits
		TS_ASSERT_EQUALS(dosCdGet32(request + 14), 75u * 60 * 74 - 150);
	}

	void test_stop_request_layout() {
		uint8 request[kDosCdHeaderSize];
		memset(request, 0xEE, sizeof(request));
		TS_ASSERT_EQUALS(dosCdBuildStop(request), 13);
		TS_ASSERT_EQUALS(request[0], 13);
		TS_ASSERT_EQUALS(request[2], 0x85);
		for (int i = 3; i < 13; i++)
			TS_ASSERT_EQUALS(request[i], 0);
	}

	void test_ioctl_request_layout() {
		uint8 request[kDosCdIoctlRequestSize];
		memset(request, 0xEE, sizeof(request));
		TS_ASSERT_EQUALS(dosCdBuildIoctl(request, false, 0x1234, 0x0005, 7), 26);
		TS_ASSERT_EQUALS(request[0], 26);
		TS_ASSERT_EQUALS(request[2], 3);                         // IOCTL input
		TS_ASSERT_EQUALS(dosCdGet16(request + 14), 0x0005);      // the control block's far pointer: offset...
		TS_ASSERT_EQUALS(dosCdGet16(request + 16), 0x1234);      // ...then segment
		TS_ASSERT_EQUALS(dosCdGet16(request + 18), 7);           // its size
		TS_ASSERT_EQUALS(dosCdGet16(request + 20), 0);
		TS_ASSERT_EQUALS(dosCdGet32(request + 22), 0u);

		dosCdBuildIoctl(request, true, 0x1234, 0x0005, 9);
		TS_ASSERT_EQUALS(request[2], 12);                        // IOCTL output
		TS_ASSERT_EQUALS(dosCdGet16(request + 18), 9);
	}

	void test_request_header() {
		uint8 request[kDosCdPlayRequestSize];
		memset(request, 0xEE, sizeof(request));
		dosCdFillHeader(request, 16, 0x84);
		TS_ASSERT_EQUALS(request[0], 16);                        // the length of the whole request
		TS_ASSERT_EQUALS(request[1], 0);                         // the subunit
		TS_ASSERT_EQUALS(request[2], 0x84);                      // the command
		for (int i = 3; i < 16; i++)                             // status and reserved, cleared
			TS_ASSERT_EQUALS(request[i], 0);
		TS_ASSERT_EQUALS(request[16], 0xEE);                     // past the length: left alone
	}

	void test_status_word() {
		TS_ASSERT(!dosCdStatusFailed(0x0100));                   // done
		TS_ASSERT(dosCdStatusFailed(0x8102));                    // error bit, "not ready"
		TS_ASSERT(dosCdStatusPlaying(0x0300));                   // done + busy
		TS_ASSERT(!dosCdStatusPlaying(0x0100));

		uint8 request[kDosCdHeaderSize] = {0};
		dosCdPut16(request + 3, 0x0300);
		TS_ASSERT_EQUALS(dosCdRequestStatus(request), 0x0300);
	}

	void test_device_status_answer() {
		uint8 block[kDosCdDeviceStatusSize];
		memset(block, 0xEE, sizeof(block));
		dosCdBuildDeviceStatus(block);
		TS_ASSERT_EQUALS(kDosCdDeviceStatusSize, 5);
		TS_ASSERT_EQUALS(block[0], 6);
		for (int i = 1; i < kDosCdDeviceStatusSize; i++)
			TS_ASSERT_EQUALS(block[i], 0);

		// A drive that plays audio and sets its volume, with no disc: bits 4, 8 and 11
		block[1] = 0x10;
		block[2] = 0x09;
		uint32 status = 0;
		TS_ASSERT(dosCdParseDeviceStatus(block, status));
		TS_ASSERT_EQUALS(status, 0x0910u);
		TS_ASSERT(status & kDosCdDevicePlaysAudio);
		TS_ASSERT(status & kDosCdDeviceSetsVolume);
		TS_ASSERT(status & kDosCdDeviceNoDisc);

		block[0] = 10;
		TS_ASSERT(!dosCdParseDeviceStatus(block, status));       // the answer to a different question
	}

	void test_audio_status_question() {
		uint8 block[kDosCdAudioStatusSize + 1];
		memset(block, 0xEE, sizeof(block));
		dosCdBuildAudioStatus(block);
		TS_ASSERT_EQUALS(kDosCdAudioStatusSize, 11);
		TS_ASSERT_EQUALS(block[0], 15);                          // IOCTL input 15: audio status
		for (int i = 1; i < kDosCdAudioStatusSize; i++)
			TS_ASSERT_EQUALS(block[i], 0);
		TS_ASSERT_EQUALS(block[kDosCdAudioStatusSize], 0xEE);
	}

	void test_a_disc_has_music_unless_all_its_tracks_are_data() {
		DosCdToc toc = threeTracks();
		TS_ASSERT(toc.hasMusic());
		toc.data[1] = true;                                      // a game CD: data, then music
		TS_ASSERT(toc.hasMusic());
		toc.data[2] = toc.data[3] = true;                        // a data CD
		TS_ASSERT(!toc.hasMusic());

		DosCdToc none;
		TS_ASSERT(!none.hasMusic());                             // no disc
	}

	void test_disc_info_answer() {
		// What a three track disc answers: 0Ah, first, last, lead-out 00:12:00 (frame, second, minute, 0)
		DosCdToc toc;
		uint8 block[kDosCdDiscInfoSize];
		dosCdBuildDiscInfo(block);
		TS_ASSERT_EQUALS(block[0], 10);

		block[1] = 1;
		block[2] = 3;
		block[3] = 0;
		block[4] = 12;
		block[5] = 0;
		block[6] = 0;
		TS_ASSERT(dosCdParseDiscInfo(block, toc));
		TS_ASSERT_EQUALS(toc.first, 1);
		TS_ASSERT_EQUALS(toc.last, 3);
		TS_ASSERT_EQUALS(toc.leadOut, 12u * 75);
	}

	void test_disc_info_answers_that_are_refused() {
		DosCdToc toc;
		uint8 block[kDosCdDiscInfoSize];
		dosCdBuildDiscInfo(block);
		TS_ASSERT(!dosCdParseDiscInfo(block, toc));              // first 0: no disc

		block[1] = 5;
		block[2] = 3;
		TS_ASSERT(!dosCdParseDiscInfo(block, toc));              // last before first

		block[1] = 1;
		block[2] = 120;
		TS_ASSERT(!dosCdParseDiscInfo(block, toc));              // more than 99 tracks

		block[0] = 11;
		block[2] = 3;
		TS_ASSERT(!dosCdParseDiscInfo(block, toc));              // the answer to a different question
	}

	void test_track_info_answer() {
		DosCdToc toc;
		uint8 block[kDosCdTrackInfoSize];
		dosCdBuildTrackInfo(block, 2);
		TS_ASSERT_EQUALS(block[0], 11);
		TS_ASSERT_EQUALS(block[1], 2);

		// Track 2 at 00:08:00, a music track
		block[2] = 0;
		block[3] = 8;
		block[4] = 0;
		block[5] = 0;
		block[6] = 0x00;
		TS_ASSERT(dosCdParseTrackInfo(block, 2, toc));
		TS_ASSERT_EQUALS(toc.start[2], 8u * 75);
		TS_ASSERT(!toc.data[2]);

		// The same with the control byte of a data track
		block[6] = 0x40;
		TS_ASSERT(dosCdParseTrackInfo(block, 2, toc));
		TS_ASSERT(toc.data[2]);

		// The answer for another track than the one asked for
		TS_ASSERT(!dosCdParseTrackInfo(block, 3, toc));
	}

	void test_volume_balance_like_the_mixer() {
		byte left, right;
		dosCdChannelVolumes(200, 0, left, right);
		TS_ASSERT_EQUALS(left, 200);
		TS_ASSERT_EQUALS(right, 200);

		dosCdChannelVolumes(200, -127, left, right);             // all the way to the left
		TS_ASSERT_EQUALS(left, 200);
		TS_ASSERT_EQUALS(right, 0);

		dosCdChannelVolumes(200, 127, left, right);              // all the way to the right
		TS_ASSERT_EQUALS(left, 0);
		TS_ASSERT_EQUALS(right, 200);

		dosCdChannelVolumes(254, -64, left, right);              // half way to the left
		TS_ASSERT_EQUALS(left, 254);
		TS_ASSERT_EQUALS(right, 126);

		dosCdChannelVolumes(255, 0, left, right);                // full volume stays 255, never wraps
		TS_ASSERT_EQUALS(left, 255);
		TS_ASSERT_EQUALS(right, 255);
	}

	void test_a_balance_past_full_left_is_full_left() {
		byte left, right;
		dosCdChannelVolumes(255, -128, left, right);             // the one value an int8 has below -127
		TS_ASSERT_EQUALS(left, 255);
		TS_ASSERT_EQUALS(right, 0);
	}

	void test_channel_control_block() {
		uint8 block[kDosCdChannelsSize];
		dosCdBuildChannels(block, 100, 50);
		TS_ASSERT_EQUALS(block[0], 3);                           // audio channel control
		TS_ASSERT_EQUALS(block[1], 0);
		TS_ASSERT_EQUALS(block[2], 100);   // output 0 plays input 0 at the left volume
		TS_ASSERT_EQUALS(block[3], 1);
		TS_ASSERT_EQUALS(block[4], 50);    // output 1 plays input 1 at the right volume
		TS_ASSERT_EQUALS(block[5], 2);
		TS_ASSERT_EQUALS(block[6], 0);     // the other two outputs are silent
		TS_ASSERT_EQUALS(block[7], 3);
		TS_ASSERT_EQUALS(block[8], 0);
	}

	void test_a_single_play() {
		DosCdLoop loop;
		TS_ASSERT_EQUALS(dosCdLoopStep(loop, 0, false), kDosCdDone);       // nothing was started

		loop.begin(1, 1000, 75);                                           // one second, from t=1000
		TS_ASSERT_EQUALS(loop.endTime, 2000u);
		TS_ASSERT(dosCdLoopPlaying(loop, 1500, false));
		TS_ASSERT_EQUALS(dosCdLoopStep(loop, 1500, true), kDosCdWait);
		TS_ASSERT(!dosCdLoopPlaying(loop, 2000, false));
		TS_ASSERT_EQUALS(dosCdLoopStep(loop, 2000, false), kDosCdDone);
		TS_ASSERT_EQUALS(loop.loops, 0);
	}

	void test_zero_loops_means_once() {
		DosCdLoop loop;
		loop.begin(0, 0, 75);
		TS_ASSERT_EQUALS(loop.loops, 1);
	}

	void test_several_plays() {
		DosCdLoop loop;
		loop.begin(3, 0, 75);
		TS_ASSERT_EQUALS(dosCdLoopStep(loop, 1000, false), kDosCdReplay);
		TS_ASSERT_EQUALS(loop.loops, 2);
		loop.endTime = 2000;
		TS_ASSERT_EQUALS(dosCdLoopStep(loop, 2000, false), kDosCdReplay);
		TS_ASSERT_EQUALS(loop.loops, 1);
		loop.endTime = 3000;
		TS_ASSERT_EQUALS(dosCdLoopStep(loop, 3000, false), kDosCdDone);
		TS_ASSERT_EQUALS(loop.loops, 0);
	}

	void test_for_ever() {
		DosCdLoop loop;
		loop.begin(-1, 0, 75);
		for (int i = 1; i <= 50; i++) {
			TS_ASSERT_EQUALS(dosCdLoopStep(loop, i * 1000, false), kDosCdReplay);
			TS_ASSERT_EQUALS(loop.loops, -1);
			loop.endTime = (i + 1) * 1000;
		}
	}

	void test_a_late_drive_is_given_time_but_not_for_ever() {
		DosCdLoop loop;
		loop.begin(1, 0, 75);

		// The drive still says it plays at the end time: half a second more, six times
		uint32 now = 1000;
		for (int i = 0; i < 6; i++) {
			TS_ASSERT_EQUALS(dosCdLoopStep(loop, now, true), kDosCdWait);
			TS_ASSERT_EQUALS(loop.endTime, now + 500);
			now += 500;
		}
		// then it is taken to be over, whatever the drive says
		TS_ASSERT_EQUALS(dosCdLoopStep(loop, now, true), kDosCdDone);
		TS_ASSERT_EQUALS(loop.loops, 0);
	}

	void test_the_clock_wrapping_around() {
		DosCdLoop loop;
		loop.begin(1, 0xFFFFFF00u, 75);                          // ends after the 32 bit clock wraps
		TS_ASSERT(dosCdLoopPlaying(loop, 0xFFFFFF80u, false));
		TS_ASSERT_EQUALS(dosCdLoopStep(loop, 0xFFFFFF80u, false), kDosCdWait);
		TS_ASSERT_EQUALS(dosCdLoopStep(loop, 0x00000300u, false), kDosCdDone);
	}

	void test_cancel() {
		DosCdLoop loop;
		loop.begin(-1, 0, 75);
		loop.cancel();
		TS_ASSERT(!dosCdLoopPlaying(loop, 10, true));
		TS_ASSERT_EQUALS(dosCdLoopStep(loop, 5000, true), kDosCdDone);
	}

	void test_drive_letters_from_the_configuration() {
		TS_ASSERT_EQUALS(dosCdDriveLetter("D"), 3);
		TS_ASSERT_EQUALS(dosCdDriveLetter("d:"), 3);
		TS_ASSERT_EQUALS(dosCdDriveLetter("E:\\"), 4);
		TS_ASSERT_EQUALS(dosCdDriveLetter("e:/"), 4);
		TS_ASSERT_EQUALS(dosCdDriveLetter("A"), 0);
		TS_ASSERT_EQUALS(dosCdDriveLetter("Z:"), 25);
		TS_ASSERT_EQUALS(dosCdDriveLetter("0"), -1);
		TS_ASSERT_EQUALS(dosCdDriveLetter(""), -1);
		TS_ASSERT_EQUALS(dosCdDriveLetter("/dev/cdrom"), -1);
		TS_ASSERT_EQUALS(dosCdDriveLetter("CD"), -1);
		TS_ASSERT_EQUALS(dosCdDriveLetter("D:foo"), -1);
		TS_ASSERT_EQUALS(dosCdDriveLetter(nullptr), -1);
	}

	void test_the_mixer_of_the_card_in_blaster() {
		unsigned base = 1;
		TS_ASSERT_EQUALS(dosCdBlasterMixer("A220 I5 D1 H5 P330 T6", base), kDosCdMixerSb16);
		TS_ASSERT_EQUALS(base, 0x220u);
		TS_ASSERT_EQUALS(dosCdBlasterMixer("A240 I7 D3 T4", base), kDosCdMixerSbPro);
		TS_ASSERT_EQUALS(base, 0x240u);
		TS_ASSERT_EQUALS(dosCdBlasterMixer("A220 I7 D1 T2", base), kDosCdMixerSbPro);
		TS_ASSERT_EQUALS(dosCdBlasterMixer("A220 I7 D1 T5", base), kDosCdMixerSbPro);
		TS_ASSERT_EQUALS(dosCdBlasterMixer("A220 I7 D1 H5", base), kDosCdMixerSb16);     // a 16-bit channel and no type
		TS_ASSERT_EQUALS(dosCdBlasterMixer("  a2a0  i5 d1  t6 ", base), kDosCdMixerSb16); // any case, spaces
		TS_ASSERT_EQUALS(base, 0x2A0u);
	}

	void test_cards_without_a_known_mixer() {
		unsigned base = 1;
		TS_ASSERT_EQUALS(dosCdBlasterMixer("A220 I7 D1 T3", base), kDosCdNoMixer);       // SB 2.0
		TS_ASSERT_EQUALS(dosCdBlasterMixer("A220 I7 D1 T1", base), kDosCdNoMixer);
		TS_ASSERT_EQUALS(dosCdBlasterMixer("A220 I7 D1", base), kDosCdNoMixer);          // no type
		TS_ASSERT_EQUALS(dosCdBlasterMixer("I5 D1 H5 T6", base), kDosCdNoMixer);         // no port
		TS_ASSERT_EQUALS(base, 0u);
		TS_ASSERT_EQUALS(dosCdBlasterMixer("A220 H T4", base), kDosCdMixerSbPro);         // an H with no channel
		TS_ASSERT_EQUALS(dosCdBlasterMixer("", base), kDosCdNoMixer);
		TS_ASSERT_EQUALS(dosCdBlasterMixer(nullptr, base), kDosCdNoMixer);
	}

	void test_a_silent_sb16_cd_input_is_raised_to_the_voice_level() {
		uint8 level = 0;
		TS_ASSERT(dosCdSb16CdLevel(0, 0, 0xF8, level));
		TS_ASSERT_EQUALS(level, 0xF8);
		TS_ASSERT(dosCdSb16CdLevel(0x07, 0x05, 0xA5, level));   // bits 2-0 are not part of the level
		TS_ASSERT_EQUALS(level, 0xA0);
		TS_ASSERT(dosCdSb16CdLevel(0, 0, 0x07, level));          // the voice silent too: 24 of 31
		TS_ASSERT_EQUALS(level, 24 << 3);
	}

	void test_an_sb16_cd_input_the_user_set_is_left_alone() {
		uint8 level = 0x55;
		TS_ASSERT(!dosCdSb16CdLevel(0x08, 0, 0xF8, level));      // the left side at 1 of 31
		TS_ASSERT(!dosCdSb16CdLevel(0, 0x10, 0xF8, level));
		TS_ASSERT(!dosCdSb16CdLevel(0xC0, 0xC0, 0xF8, level));
		TS_ASSERT_EQUALS(level, 0x55);
	}

	void test_sb16_output_switches() {
		TS_ASSERT_EQUALS(dosCdSb16Switches(0x00), 0x06);         // nothing on: the CD's two sides on
		TS_ASSERT_EQUALS(dosCdSb16Switches(0x19), 0x1F);         // line and microphone kept
		TS_ASSERT_EQUALS(dosCdSb16Switches(0x04), 0x04);         // one side on: as the user set it
		TS_ASSERT_EQUALS(dosCdSb16Switches(0x02), 0x02);
		TS_ASSERT_EQUALS(dosCdSb16Switches(0x1F), 0x1F);
	}

	void test_a_silent_sb_pro_cd_input_is_raised_to_the_voice_level() {
		uint8 level = 0;
		TS_ASSERT(dosCdSbProCdLevel(0x00, 0x99, level));
		TS_ASSERT_EQUALS(level, 0x99);
		TS_ASSERT(dosCdSbProCdLevel(0x11, 0x99, level));         // bits 4 and 0 are not part of the levels
		TS_ASSERT(dosCdSbProCdLevel(0x00, 0x11, level));         // the voice silent too
		TS_ASSERT_EQUALS(level, 0xCC);

		TS_ASSERT(!dosCdSbProCdLevel(0x31, 0x99, level));        // the left side at 1 of 7
		TS_ASSERT(!dosCdSbProCdLevel(0x13, 0x99, level));        // the right side at 1 of 7
		TS_ASSERT(!dosCdSbProCdLevel(0xFF, 0x99, level));
	}
};
