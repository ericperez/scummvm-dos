/* Decode one MP3, Ogg Vorbis or FLAC file with the cross-built DJGPP libraries
 * (libmad, Tremor, libFLAC) and write signed 16-bit little-endian PCM, so the
 * result can be compared with a host decoder.
 *
 *   CODEC mp3|ogg|flac <input> <output.raw>
 *
 * Build: see build-codectest.sh, next to this file
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mad.h>
#include <tremor/ivorbisfile.h>
#define FLAC__NO_DLL
#include <FLAC/stream_decoder.h>

static FILE *out;
static unsigned long totalSamples;

static short scale16(mad_fixed_t sample) {
	sample += (1L << (MAD_F_FRACBITS - 16)); /* round */
	if (sample >= MAD_F_ONE)
		sample = MAD_F_ONE - 1;
	else if (sample < -MAD_F_ONE)
		sample = -MAD_F_ONE;
	return (short)(sample >> (MAD_F_FRACBITS + 1 - 16));
}

static int decodeMp3(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f)
		return 1;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = (unsigned char *)malloc(len + MAD_BUFFER_GUARD);
	if (!buf || fread(buf, 1, len, f) != (size_t)len) {
		free(buf);
		fclose(f);
		return 1;
	}
	memset(buf + len, 0, MAD_BUFFER_GUARD);
	fclose(f);

	struct mad_stream stream;
	struct mad_frame frame;
	struct mad_synth synth;
	mad_stream_init(&stream);
	mad_frame_init(&frame);
	mad_synth_init(&synth);
	mad_stream_buffer(&stream, buf, len + MAD_BUFFER_GUARD);

	unsigned frames = 0, rate = 0, channels = 0;
	for (;;) {
		if (mad_frame_decode(&frame, &stream)) {
			if (MAD_RECOVERABLE(stream.error))
				continue;
			break;
		}
		mad_synth_frame(&synth, &frame);
		rate = frame.header.samplerate;
		channels = synth.pcm.channels;
		for (unsigned i = 0; i < synth.pcm.length; i++) {
			for (unsigned c = 0; c < channels; c++) {
				short s = scale16(synth.pcm.samples[c][i]);
				fwrite(&s, 2, 1, out);
				totalSamples++;
			}
		}
		frames++;
	}
	printf("mp3: %u frames, %u Hz, %u channels, %lu samples\n", frames, rate, channels, totalSamples);
	mad_synth_finish(&synth);
	mad_frame_finish(&frame);
	mad_stream_finish(&stream);
	free(buf);
	return frames ? 0 : 1;
}

static int decodeOgg(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f)
		return 1;
	OggVorbis_File vf;
	if (ov_open(f, &vf, NULL, 0) < 0) {
		printf("ogg: ov_open failed\n");
		fclose(f);
		return 1;
	}
	vorbis_info *vi = ov_info(&vf, -1);
	printf("ogg: %ld Hz, %d channels\n", vi->rate, vi->channels);

	char pcm[4096];
	int section;
	long n;
	while ((n = ov_read(&vf, pcm, sizeof(pcm), &section)) > 0) {
		fwrite(pcm, 1, n, out);
		totalSamples += n / 2;
	}
	printf("ogg: %lu samples\n", totalSamples);
	ov_clear(&vf);
	return totalSamples ? 0 : 1;
}

static FLAC__StreamDecoderWriteStatus flacWrite(const FLAC__StreamDecoder *dec, const FLAC__Frame *frame,
        const FLAC__int32 *const buffer[], void *data) {
	(void)dec;
	(void)data;
	for (unsigned i = 0; i < frame->header.blocksize; i++) {
		for (unsigned c = 0; c < frame->header.channels; c++) {
			short s = (short)buffer[c][i];
			fwrite(&s, 2, 1, out);
			totalSamples++;
		}
	}
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void flacMeta(const FLAC__StreamDecoder *dec, const FLAC__StreamMetadata *meta, void *data) {
	(void)dec;
	(void)data;
	if (meta->type == FLAC__METADATA_TYPE_STREAMINFO)
		printf("flac: %u Hz, %u channels, %u bits, %lu total samples\n", meta->data.stream_info.sample_rate,
		       meta->data.stream_info.channels, meta->data.stream_info.bits_per_sample,
		       (unsigned long)meta->data.stream_info.total_samples);
}

static void flacError(const FLAC__StreamDecoder *dec, FLAC__StreamDecoderErrorStatus status, void *data) {
	(void)dec;
	(void)data;
	printf("flac: error %d\n", (int)status);
}

static int decodeFlac(const char *path) {
	FLAC__StreamDecoder *dec = FLAC__stream_decoder_new();
	if (FLAC__stream_decoder_init_file(dec, path, flacWrite, flacMeta, flacError, NULL) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		printf("flac: init failed\n");
		return 1;
	}
	FLAC__bool ok = FLAC__stream_decoder_process_until_end_of_stream(dec);
	printf("flac: %lu samples, %s\n", totalSamples, ok ? "ok" : "decode error");
	FLAC__stream_decoder_delete(dec);
	return ok ? 0 : 1;
}

int main(int argc, char **argv) {
	if (argc < 4) {
		printf("usage: CODEC mp3|ogg|flac input output.raw\n");
		return 2;
	}
	out = fopen(argv[3], "wb");
	if (!out)
		return 2;
	int rc;
	if (!strcmp(argv[1], "mp3"))
		rc = decodeMp3(argv[2]);
	else if (!strcmp(argv[1], "ogg"))
		rc = decodeOgg(argv[2]);
	else
		rc = decodeFlac(argv[2]);
	fclose(out);
	return rc;
}
