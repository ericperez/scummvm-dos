#!/bin/sh
# Cross-compile libmad, libogg, Tremor (libvorbisidec) and libFLAC (decoder only)
# for 32-bit MS-DOS with DJGPP, targeting the 486, by hand (no configure scripts).
#
#   sources   $L/src/<lib>    (extracted tarballs, never modified, never built in)
#   objects   $L/build/<lib>
#   installed $L/prefix/{include,lib}
#
# Usage: build-libs.sh [libmad] [libogg] [tremor] [flac]   (default: all)
set -e

# The tool locations (DJGPP_HOME, DJGPP) come from dos-env.sh, the same as for configure-dos.sh
DEPS_DIR=$(cd "$(dirname "$0")" && pwd)
DOS_DIR=$(cd "$DEPS_DIR/.." && pwd)
. "$DOS_DIR/dos-env.sh"
dos_env || exit 1
L=${DEPS_ROOT:-$SRC/build-dos-deps}

CC=i586-pc-msdosdjgpp-gcc
AR=i586-pc-msdosdjgpp-ar
RANLIB=i586-pc-msdosdjgpp-ranlib
CFLAGS="-O2 -march=i486 -mtune=i486 -fomit-frame-pointer"
P=$L/prefix

mkdir -p "$P/include" "$P/lib"

# compile <srcdir> <builddir> <extra flags> files...
compile() {
  src=$1; bld=$2; flags=$3; shift 3
  mkdir -p "$bld"
  for f in "$@"; do
    o="$bld/$(basename "$f" .c).o"
    $CC $CFLAGS $flags -c "$src/$f" -o "$o"
  done
}

build_libmad() {
  SRC=$L/src/libmad; B=$L/build/libmad
  mkdir -p "$B"
  printf '%s\n' \
    '#define SIZEOF_INT 4' \
    '#define SIZEOF_LONG 4' \
    '#define SIZEOF_LONG_LONG 8' \
    '#define STDC_HEADERS 1' \
    '#define HAVE_ASSERT_H 1' \
    '#define HAVE_LIMITS_H 1' > "$B/config.h"

  # mad.h is assembled the way libmad's own Makefile does it, with the
  # fixed-point multiply for Intel chosen (FPM_INTEL).
  {
    sed -e '1s|.*|/*|' -e '1b' -e '$s|.*| */|' -e '$b' -e 's/^.*/ *&/' "$SRC/COPYRIGHT"
    echo
    echo "# ifdef __cplusplus"
    echo 'extern "C" {'
    echo "# endif"
    echo
    echo "# define FPM_INTEL"
    echo
    sed -ne 's/^# *define  *\(SIZEOF_.*\)/# define \1/p' "$B/config.h"
    echo
    for header in version.h fixed.h bit.h timer.h stream.h frame.h synth.h decoder.h; do
      echo
      sed -n -f "$SRC/mad.h.sed" "$SRC/$header"
    done
    echo
    echo "# ifdef __cplusplus"
    echo '}'
    echo "# endif"
  } > "$B/mad.h"

  compile "$SRC" "$B" "-DHAVE_CONFIG_H -DFPM_INTEL -I$B -I$SRC" \
    version.c fixed.c bit.c timer.c stream.c frame.c synth.c decoder.c layer12.c layer3.c huffman.c
  rm -f "$B/libmad.a"
  $AR rcs "$B/libmad.a" "$B"/version.o "$B"/fixed.o "$B"/bit.o "$B"/timer.o "$B"/stream.o "$B"/frame.o \
    "$B"/synth.o "$B"/decoder.o "$B"/layer12.o "$B"/layer3.o "$B"/huffman.o
  $RANLIB "$B/libmad.a"
  cp "$B/mad.h" "$P/include/mad.h"
  cp "$B/libmad.a" "$P/lib/libmad.a"
  echo "libmad: done"
}

build_libogg() {
  SRC=$L/src/libogg; B=$L/build/libogg
  mkdir -p "$B/include/ogg"
  printf '%s\n' \
    '#ifndef __CONFIG_TYPES_H__' \
    '#define __CONFIG_TYPES_H__' \
    '#include <stdint.h>' \
    'typedef int16_t ogg_int16_t;' \
    'typedef uint16_t ogg_uint16_t;' \
    'typedef int32_t ogg_int32_t;' \
    'typedef uint32_t ogg_uint32_t;' \
    'typedef int64_t ogg_int64_t;' \
    'typedef uint64_t ogg_uint64_t;' \
    '#endif' > "$B/include/ogg/config_types.h"
  cp "$SRC/include/ogg/ogg.h" "$SRC/include/ogg/os_types.h" "$B/include/ogg/"

  compile "$SRC/src" "$B" "-I$B/include" bitwise.c framing.c
  rm -f "$B/libogg.a"
  $AR rcs "$B/libogg.a" "$B/bitwise.o" "$B/framing.o"
  $RANLIB "$B/libogg.a"
  mkdir -p "$P/include/ogg"
  cp "$B"/include/ogg/*.h "$P/include/ogg/"
  # libogg's DJGPP section of os_types.h defines ogg_int16_t but forgets
  # ogg_uint16_t, which libtheora uses
  sed -e '/^#elif defined (DJGPP)/,/^#elif defined(R5900)/ s/^   typedef short ogg_int16_t;$/   typedef short ogg_int16_t; typedef unsigned short ogg_uint16_t;/' \
    "$P/include/ogg/os_types.h" > "$P/include/ogg/os_types.h.new"
  mv "$P/include/ogg/os_types.h.new" "$P/include/ogg/os_types.h"
  cp "$B/libogg.a" "$P/lib/libogg.a"
  echo "libogg: done"
}

build_tremor() {
  SRC=$L/src/tremor; B=$L/build/tremor
  # DJGPP's headers define no BYTE_ORDER; Tremor's misc.h needs one (x86 = little endian)
  compile "$SRC" "$B" "-I$P/include -I$SRC -DONLY_C -DLITTLE_ENDIAN=1234 -DBIG_ENDIAN=4321 -DBYTE_ORDER=1234" \
    mdct.c block.c window.c synthesis.c info.c floor1.c floor0.c vorbisfile.c res012.c mapping0.c \
    registry.c codebook.c sharedbook.c
  rm -f "$B/libvorbisidec.a"
  $AR rcs "$B/libvorbisidec.a" "$B"/*.o
  $RANLIB "$B/libvorbisidec.a"
  mkdir -p "$P/include/tremor"
  cp "$SRC/ivorbiscodec.h" "$SRC/ivorbisfile.h" "$SRC/config_types.h" "$P/include/tremor/"
  cp "$B/libvorbisidec.a" "$P/lib/libvorbisidec.a"
  echo "tremor: done"
}

build_flac() {
  SRC=$L/src/flac; B=$L/build/flac
  mkdir -p "$B/include"
  printf '%s\n' \
    '#define CPU_IS_BIG_ENDIAN 0' \
    '#define ENABLE_64_BIT_WORDS 0' \
    '#define OGG_FOUND 0' \
    '#define FLAC__HAS_OGG 0' \
    '#define FLAC__HAS_X86INTRIN 0' \
    '#define FLAC__HAS_NEONINTRIN 0' \
    '#define FLAC__HAS_A64NEONINTRIN 0' \
    '#define HAVE_INTTYPES_H 1' \
    '#define HAVE_STDINT_H 1' \
    '#define HAVE_LROUND 1' \
    '#define PACKAGE_VERSION "1.4.3"' \
    '#define PACKAGE_NAME "flac"' \
    '#define VERSION "1.4.3"' > "$B/include/config.h"

  compile "$SRC/src/libFLAC" "$B" \
    "-DHAVE_CONFIG_H -DFLAC__NO_DLL -DFLAC__NO_ASM -I$B/include -I$SRC/include -I$SRC/src/libFLAC/include" \
    bitmath.c bitreader.c cpu.c crc.c fixed.c float.c format.c lpc.c md5.c memory.c \
    metadata_object.c stream_decoder.c
  rm -f "$B/libFLAC.a"
  $AR rcs "$B/libFLAC.a" "$B"/*.o
  $RANLIB "$B/libFLAC.a"
  mkdir -p "$P/include/FLAC"
  cp "$SRC"/include/FLAC/*.h "$P/include/FLAC/"
  cp "$B/libFLAC.a" "$P/lib/libFLAC.a"
  echo "flac: done"
}

if [ $# -eq 0 ]; then
  set -- libmad libogg tremor flac
fi
for what in "$@"; do
  "build_$what"
done
