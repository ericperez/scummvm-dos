#!/bin/sh
# Cross-compile the audio decoding libraries that ScummVM can use but the DOS
# builds did not have, for 32-bit MS-DOS with DJGPP, targeting the 486.
#
#   liba52      0.7.4   https://ftp.osuosl.org/pub/blfs/conglomeration/a52dec/a52dec-0.7.4.tar.gz
#                       (AC-3 audio: the Zork games on DVD, via the ZVision engine)
#   libmpcdec   1.2.6   https://files.musepack.net/source/libmpcdec-1.2.6.tar.bz2
#                       (Musepack audio: the QD engine's music)
#   faad2       2.10.1  https://github.com/knik0/faad2/archive/refs/tags/2.10.1.tar.gz
#                       (AAC audio, in QuickTime files: Mohawk, Director and other engines)
#
# Same layout as build-libs.sh: sources in $L/src, objects in $L/build, installed in $L/prefix.
#
# faad2 is compiled by hand (no configure script), in fixed point: no FPU needed.
#
# Usage: build-audio-libs.sh [a52] [mpcdec] [faad]   (default: all)
set -e

# The tool locations (DJGPP_HOME, DJGPP) come from dos-env.sh, the same as for configure-dos.sh
DEPS_DIR=$(cd "$(dirname "$0")" && pwd)
DOS_DIR=$(cd "$DEPS_DIR/.." && pwd)
. "$DOS_DIR/dos-env.sh"
dos_env || exit 1
L=${DEPS_ROOT:-$SRC/build-dos-deps}

HOST=i586-pc-msdosdjgpp
CC=$HOST-gcc
AR=$HOST-ar
RANLIB=$HOST-ranlib
CFLAGS="-O2 -march=i486 -mtune=i486 -fomit-frame-pointer"
P=$L/prefix
JOBS=${JOBS:-8}

mkdir -p "$P/include" "$P/lib"

# All three are compiled by hand: the downloaded packages' own configure scripts
# are never run, only the cross compiler is, on the C files.

build_a52() {
  SRC=$L/src/a52dec-0.7.4; B=$L/build/a52dec-0.7.4
  rm -rf "$B"; mkdir -p "$B"
  # What configure would have written; no memalign, no alignment attributes
  # (DJGPP's object format cannot align above 4 bytes), single precision
  printf '#define HAVE_INTTYPES_H 1\n#define HAVE_STDINT_H 1\n#define HAVE_STDLIB_H 1\n#define HAVE_STRING_H 1\n#define STDC_HEADERS 1\n#define PACKAGE "a52dec"\n#define VERSION "0.7.4"\n' > "$B/config.h"
  for f in bitstream imdct bit_allocate parse downmix; do
    $CC $CFLAGS -DHAVE_CONFIG_H -I"$B" -I"$SRC/include" -I"$SRC/liba52" -c "$SRC/liba52/$f.c" -o "$B/$f.o"
  done
  rm -f "$B/liba52.a"
  $AR rcs "$B/liba52.a" "$B"/*.o
  $RANLIB "$B/liba52.a"
  mkdir -p "$P/include/a52dec"
  cp "$SRC/include/a52.h" "$SRC/include/attributes.h" "$SRC/include/mm_accel.h" "$P/include/a52dec/"
  cp "$B/liba52.a" "$P/lib/liba52.a"
  echo "liba52: done"
}

build_mpcdec() {
  SRC=$L/src/libmpcdec-1.2.6; B=$L/build/libmpcdec-1.2.6
  rm -rf "$B"; mkdir -p "$B"
  for f in huffsv46 huffsv7 idtag mpc_decoder mpc_reader requant streaminfo synth_filter; do
    $CC $CFLAGS -I"$SRC/include" -I"$SRC/src" -c "$SRC/src/$f.c" -o "$B/$f.o"
  done
  rm -f "$B/libmpcdec.a"
  $AR rcs "$B/libmpcdec.a" "$B"/*.o
  $RANLIB "$B/libmpcdec.a"
  mkdir -p "$P/include/mpcdec"
  cp "$SRC"/include/mpcdec/*.h "$P/include/mpcdec/"
  cp "$B/libmpcdec.a" "$P/lib/libmpcdec.a"
  echo "libmpcdec: done"
}

build_faad() {
  SRC=$L/src/faad2-2.10.1/libfaad; B=$L/build/faad2-2.10.1
  rm -rf "$B"; mkdir -p "$B"
  FILES="bits cfft common decoder drc drm_dec error filtbank hcr huffman ic_predict is lt_predict mdct mp4 ms output pns \
    ps_dec ps_syntax pulse rvlc sbr_dct sbr_dec sbr_e_nf sbr_fbt sbr_hfadj sbr_hfgen sbr_huff sbr_qmf sbr_syntax \
    sbr_tf_grid specrec ssr ssr_fb ssr_ipqf syntax tns"
  for f in $FILES; do
    $CC $CFLAGS -DFIXED_POINT -DSTDC_HEADERS=1 -DHAVE_STDINT_H=1 -DHAVE_INTTYPES_H=1 -DHAVE_STRING_H=1 -DHAVE_STDLIB_H=1 -DHAVE_MEMCPY=1 -DHAVE_LRINTF=1 -DPACKAGE_VERSION=\"2.10.1\" -I"$SRC" -I"$L/src/faad2-2.10.1/include" \
      -c "$SRC/$f.c" -o "$B/$f.o"
  done
  rm -f "$B/libfaad.a"
  $AR rcs "$B/libfaad.a" "$B"/*.o
  $RANLIB "$B/libfaad.a"
  cp "$L/src/faad2-2.10.1/include/neaacdec.h" "$P/include/"
  cp "$B/libfaad.a" "$P/lib/libfaad.a"
  echo "faad2: done"
}

if [ $# -eq 0 ]; then
  set -- a52 mpcdec faad
fi
for what in "$@"; do
  "build_$what"
done
