/* DOS test of the libraries built by build-image-libs.sh: each one is used for
 * real, not just linked. Run it under DOS (or DOSBox-X); every line ends in PASS
 * or FAIL and the exit code is the number of failures.
 *
 * libtest-data.h holds a small picture in three formats (see mkimgs.py).
 */
#include <stdint.h> /* libmpeg2's header expects the caller to have this */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zlib.h"
#include "png.h"
#include "jpeglib.h"
#include "gif_lib.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include "mpeg2dec/mpeg2.h"
#include "theora/theoradec.h"

#include "libtest-data.h"

static int failures = 0;

static void report(const char *what, int ok, const char *detail) {
	printf("%-10s %s  %s\n", what, ok ? "PASS" : "FAIL", detail);
	if (!ok)
		failures++;
}

static int isRed(int r, int g, int b) {
	return r > 190 && g < 70 && b < 70;
}
static int isBlue(int r, int g, int b) {
	return b > 190 && r < 70 && g < 70;
}

static void testZlib(void) {
	const char *text = "DOS DOS DOS DOS DOS DOS DOS DOS ScummVM ScummVM ScummVM ScummVM";
	unsigned char packed[256], back[256];
	uLongf plen = sizeof(packed), blen = sizeof(back);
	int ok = compress(packed, &plen, (const Bytef *)text, strlen(text) + 1) == Z_OK &&
	         uncompress(back, &blen, packed, plen) == Z_OK &&
	         blen == strlen(text) + 1 && strcmp((char *)back, text) == 0;
	char d[80];
	snprintf(d, sizeof(d), "zlib %s, %u bytes -> %u -> %u", zlibVersion(), (unsigned)(strlen(text) + 1), (unsigned)plen, (unsigned)blen);
	report("zlib", ok, d);
}

static void testPng(void) {
	png_image img;
	memset(&img, 0, sizeof(img));
	img.version = PNG_IMAGE_VERSION;
	int ok = 0;
	char d[80] = "could not read";
	if (png_image_begin_read_from_memory(&img, test_png, test_png_len)) {
		img.format = PNG_FORMAT_RGB;
		unsigned char *buf = (unsigned char *)malloc(PNG_IMAGE_SIZE(img));
		if (buf && png_image_finish_read(&img, NULL, buf, 0, NULL)) {
			int w = img.width;
			ok = w == 8 && img.height == 8 && isRed(buf[0], buf[1], buf[2]) &&
			     isBlue(buf[(w - 1) * 3], buf[(w - 1) * 3 + 1], buf[(w - 1) * 3 + 2]);
			snprintf(d, sizeof(d), "libpng %s, %ux%u, left (%d,%d,%d) right (%d,%d,%d)", PNG_LIBPNG_VER_STRING,
			         (unsigned)img.width, (unsigned)img.height, buf[0], buf[1], buf[2],
			         buf[(w - 1) * 3], buf[(w - 1) * 3 + 1], buf[(w - 1) * 3 + 2]);
		}
		free(buf);
		png_image_free(&img);
	}
	report("png", ok, d);
}

static void testJpeg(void) {
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, (unsigned char *)test_jpg, test_jpg_len);
	int ok = 0;
	char d[100] = "could not read";
	if (jpeg_read_header(&cinfo, TRUE) == JPEG_HEADER_OK) {
		cinfo.out_color_space = JCS_RGB;
		jpeg_start_decompress(&cinfo);
		int w = cinfo.output_width, h = cinfo.output_height, n = cinfo.output_components;
		unsigned char *row = (unsigned char *)malloc(w * n);
		JSAMPROW rp[1];
		rp[0] = row;
		if (jpeg_read_scanlines(&cinfo, rp, 1) == 1) {
			ok = w == 8 && h == 8 && n == 3 && isRed(row[0], row[1], row[2]) &&
			     isBlue(row[(w - 1) * 3], row[(w - 1) * 3 + 1], row[(w - 1) * 3 + 2]);
			snprintf(d, sizeof(d), "libjpeg %d, %dx%d, left (%d,%d,%d) right (%d,%d,%d)", JPEG_LIB_VERSION, w, h,
			         row[0], row[1], row[2], row[(w - 1) * 3], row[(w - 1) * 3 + 1], row[(w - 1) * 3 + 2]);
		}
		free(row);
		/* leave the rest unread: that is all the test needs */
		jpeg_destroy_decompress(&cinfo);
	} else {
		jpeg_destroy_decompress(&cinfo);
	}
	report("jpeg", ok, d);
}

typedef struct {
	const unsigned char *p;
	unsigned left;
} MemSrc;

static int gifRead(GifFileType *g, GifByteType *buf, int len) {
	MemSrc *m = (MemSrc *)g->UserData;
	if ((unsigned)len > m->left)
		len = (int)m->left;
	memcpy(buf, m->p, len);
	m->p += len;
	m->left -= len;
	return len;
}

static void testGif(void) {
	MemSrc src;
	src.p = test_gif;
	src.left = test_gif_len;
	int err = 0, ok = 0;
	char d[100] = "could not read";
	GifFileType *g = DGifOpen(&src, gifRead, &err);
	if (g && DGifSlurp(g) == GIF_OK && g->ImageCount > 0) {
		SavedImage *si = &g->SavedImages[0];
		ColorMapObject *cm = si->ImageDesc.ColorMap ? si->ImageDesc.ColorMap : g->SColorMap;
		int w = si->ImageDesc.Width;
		if (cm) {
			GifColorType a = cm->Colors[si->RasterBits[0]];
			GifColorType b = cm->Colors[si->RasterBits[w - 1]];
			ok = w == 8 && si->ImageDesc.Height == 8 && isRed(a.Red, a.Green, a.Blue) && isBlue(b.Red, b.Green, b.Blue);
			snprintf(d, sizeof(d), "giflib %d.%d, %dx%d, left (%d,%d,%d) right (%d,%d,%d)", GIFLIB_MAJOR, GIFLIB_MINOR, w,
			         si->ImageDesc.Height, a.Red, a.Green, a.Blue, b.Red, b.Green, b.Blue);
		}
		DGifCloseFile(g, &err);
	}
	report("gif", ok, d);
}

static void testFreeType(void) {
	FT_Library lib;
	int ok = 0;
	char d[80] = "init failed";
	if (FT_Init_FreeType(&lib) == 0) {
		FT_Int major, minor, patch;
		FT_Library_Version(lib, &major, &minor, &patch);
		ok = major == 2;
		snprintf(d, sizeof(d), "FreeType %d.%d.%d", (int)major, (int)minor, (int)patch);
		FT_Done_FreeType(lib);
	}
	report("freetype", ok, d);
}

static void testMpeg2(void) {
	uint32_t accel = mpeg2_accel(MPEG2_ACCEL_DETECT);
	mpeg2dec_t *dec = mpeg2_init();
	char d[100];
	snprintf(d, sizeof(d), "libmpeg2, decoder %s, processor tricks 0x%x%s", dec ? "created" : "FAILED", (unsigned)accel,
	         (accel & MPEG2_ACCEL_X86_MMX) ? " (MMX)" : "");
	if (dec)
		mpeg2_close(dec);
	report("mpeg2", dec != NULL, d);
}

static void testTheora(void) {
	const char *v = th_version_string();
	th_info ti;
	th_comment tc;
	th_setup_info *ts = NULL;
	th_info_init(&ti);
	th_comment_init(&tc);
	ogg_packet op;
	memset(&op, 0, sizeof(op));
	unsigned char junk[4] = { 0, 0, 0, 0 };
	op.packet = junk;
	op.bytes = 4;
	op.b_o_s = 1;
	/* A packet that is not a Theora header must be refused, not crash */
	int rc = th_decode_headerin(&ti, &tc, &ts, &op);
	char d[100];
	snprintf(d, sizeof(d), "%s, junk header refused (code %d)", v ? v : "(no version)", rc);
	th_info_clear(&ti);
	th_comment_clear(&tc);
	th_setup_free(ts);
	report("theora", v != NULL && rc < 0, d);
}

int main(void) {
	testZlib();
	testPng();
	testJpeg();
	testGif();
	testFreeType();
	testMpeg2();
	testTheora();
	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
	return failures;
}
