#!/bin/sh
# Check that libopenmpt is cross-built for DJGPP where ScummVM's configure looks for it: the static
# library holds the functions ScummVM's tracker stream (audio/mods/universaltracker.cpp) calls, the
# headers are installed, the .pc file configure reads is there, and the library is a DJGPP (i386
# COFF) one and not a Mac one. Prints "OPENMPT LIB OK" when all of that holds.
DOS_DIR=$(cd "$(dirname "$0")/.." && pwd)
. "$DOS_DIR/dos-env.sh"
dos_env || exit 1
NM=$DJGPP_HOME/bin/i586-pc-msdosdjgpp-nm
OBJDUMP=$DJGPP_HOME/bin/i586-pc-msdosdjgpp-objdump

fail() { echo "OPENMPT LIB FAILED: $1"; exit 1; }

[ -f "$DEPS/lib/libopenmpt.a" ] || fail "no $DEPS/lib/libopenmpt.a"
[ -f "$DEPS/lib/pkgconfig/libopenmpt.pc" ] || fail "no libopenmpt.pc in $DEPS/lib/pkgconfig (configure cannot find the library)"
[ -f "$DEPS/include/libopenmpt/libopenmpt.h" ] || fail "libopenmpt.h is not installed"
[ -f "$DEPS/include/libopenmpt/libopenmpt_stream_callbacks_file.h" ] || fail "the stream callback header is not installed"

SYMBOLS=$("$NM" "$DEPS/lib/libopenmpt.a" 2>/dev/null)
[ -n "$SYMBOLS" ] || fail "nm could not read the library (not a DJGPP archive?)"
for s in openmpt_module_create2 openmpt_module_read_interleaved_stereo \
         openmpt_module_set_position_seconds openmpt_module_destroy openmpt_get_string; do
  echo "$SYMBOLS" | grep -q " T _$s\$" || fail "the library does not define $s"
done
# The stream callbacks ScummVM hands it are built in the header (a static function there)
grep -q "openmpt_stream_get_file_callbacks2(void)" "$DEPS/include/libopenmpt/libopenmpt_stream_callbacks_file.h" \
  || fail "the stream callback header lacks openmpt_stream_get_file_callbacks2"

# One object of it must be 32-bit x86 COFF (go32), the DOS format
FORMAT=$("$OBJDUMP" -f "$DEPS/lib/libopenmpt.a" 2>/dev/null | grep -m1 "file format")
case "$FORMAT" in
  *coff-go32*) ;;
  *) fail "wrong object format: $FORMAT" ;;
esac

# The pkg-config answer configure gets must point at the DOS library
LIBS=$(PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig" PKG_CONFIG_PATH= pkg-config --libs libopenmpt 2>/dev/null)
case "$LIBS" in
  *"-L$DEPS/lib"*"-lopenmpt"*) ;;
  *) fail "pkg-config answers '$LIBS'" ;;
esac

echo "OPENMPT LIB OK ($(du -k "$DEPS/lib/libopenmpt.a" | cut -f1) KB, $FORMAT)"
