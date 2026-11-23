/* Link and smoke test for the audio libraries built by build-audio-libs.sh.
 * Build:  i586-pc-msdosdjgpp-gcc -O2 -march=i486 -I prefix/include deps/tests/audiotest.c \
 *           -o AUDTEST.EXE -L prefix/lib -lmpcdec -lfaad -la52 -lm
 * Run it under DOS (DOSBox-X); it prints one line per library and "RESULT: OK".
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <a52dec/a52.h>
#include <mpcdec/mpcdec.h>
#include <neaacdec.h>

int main(void) {
	int ok = 1;

	/* AC-3: a decoder state, and a refusal of bytes that are not a frame */
	a52_state_t *a52 = a52_init(0);
	uint8_t junk[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	int flags = 0, rate = 0, bitrate = 0;
	int len = a52 ? a52_syncinfo(junk, &flags, &rate, &bitrate) : -1;
	printf("liba52: state %s, junk header %s\n", a52 ? "created" : "FAILED", len == 0 ? "refused" : "ACCEPTED");
	if (!a52 || len != 0)
		ok = 0;
	if (a52)
		a52_free(a52);

	/* AAC: open a decoder and read its capabilities, then configure it for an
	   AAC-LC stream (the 2-byte AudioSpecificConfig of 44.1 kHz stereo) */
	NeAACDecHandle aac = NeAACDecOpen();
	unsigned long caps = aac ? NeAACDecGetCapabilities() : 0;
	unsigned char asc[2] = { 0x12, 0x10 };
	unsigned long srate = 0;
	unsigned char chans = 0;
	long res = aac ? NeAACDecInit2(aac, asc, 2, &srate, &chans) : -1;
	printf("libfaad: decoder %s, capabilities 0x%lx, init %s (%lu Hz, %u channels)\n",
	       aac ? "opened" : "FAILED", caps, res == 0 ? "ok" : "FAILED", srate, (unsigned)chans);
	if (!aac || res != 0 || srate != 44100 || chans != 2)
		ok = 0;
	if (aac)
		NeAACDecClose(aac);

	/* Musepack: the stream info structure initialises */
	mpc_streaminfo info;
	mpc_streaminfo_init(&info);
	printf("libmpcdec: stream info initialised (%u Hz)\n", (unsigned)info.sample_freq);

	printf("RESULT: %s\n", ok ? "OK" : "FAILED");
	return ok ? 0 : 1;
}
