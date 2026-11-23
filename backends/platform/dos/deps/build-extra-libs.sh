#!/bin/sh
# Cross-compile two more libraries for 32-bit MS-DOS with DJGPP, targeting the 486:
#
#   FriBidi     1.0.16  https://github.com/fribidi/fribidi/releases/download/v1.0.16/fribidi-1.0.16.tar.xz
#                       (right-to-left text: the Arabic, Hebrew and Persian user interface)
#   libopenmpt  0.8.9   https://lib.openmpt.org/files/libopenmpt/src/libopenmpt-0.8.9+release.makefile.tar.gz
#                       (every tracker music format: MOD, S3M, XM, IT, MPTM and about thirty more;
#                       ScummVM's "universal tracker" stream uses it)
#   Watt-32     master  https://github.com/gvanem/Watt-32/archive/refs/heads/master.tar.gz
#                       (the TCP/IP stack and BSD sockets for DJGPP programs; talks to the
#                       network card through its packet driver. The local web server uses it.)
#
# Same layout as build-libs.sh: sources in $L/src, objects in $L/build, installed in $L/prefix.
#
# All are compiled by hand: the packages' own configure and make scripts are never run.
#
# Usage: build-extra-libs.sh [fribidi] [watt32] [openmpt]   (default: all)
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

mkdir -p "$P/include" "$P/lib"

build_fribidi() {
  SRC=$L/src/fribidi-1.0.16; B=$L/build/fribidi-1.0.16
  rm -rf "$B"; mkdir -p "$B"

  # What configure would have written
  printf '#define HAVE_INTTYPES_H 1\n#define HAVE_STDINT_H 1\n#define HAVE_STDLIB_H 1\n#define HAVE_STRING_H 1\n#define HAVE_MEMMOVE 1\n#define HAVE_MEMSET 1\n#define HAVE_STRINGIZE 1\n#define SIZEOF_INT 4\n#define SIZEOF_SHORT 2\n#define SIZEOF_VOID_P 4\n#define PACKAGE_NAME "fribidi"\n#define PACKAGE_VERSION "1.0.16"\n' > "$B/config.h"
  sed -e 's|@PACKAGE@|fribidi|; s|@PACKAGE_NAME@|fribidi|; s|@PACKAGE_BUGREPORT@|https://github.com/fribidi/fribidi/issues|' \
      -e 's|@FRIBIDI_VERSION@|1.0.16|; s|@FRIBIDI_MAJOR_VERSION@|1|; s|@FRIBIDI_MINOR_VERSION@|0|; s|@FRIBIDI_MICRO_VERSION@|16|' \
      -e 's|@FRIBIDI_INTERFACE_VERSION@|4|g; s|@SIZEOF_INT@|4|; s|@FRIBIDI_MSVC_BUILD_PLACEHOLDER@||' \
      "$SRC/lib/fribidi-config.h.in" > "$B/fribidi-config.h"

  for f in fribidi fribidi-arabic fribidi-bidi fribidi-bidi-types fribidi-brackets fribidi-char-sets \
           fribidi-char-sets-cap-rtl fribidi-char-sets-cp1255 fribidi-char-sets-cp1256 \
           fribidi-char-sets-iso8859-6 fribidi-char-sets-iso8859-8 fribidi-char-sets-utf8 \
           fribidi-deprecated fribidi-joining fribidi-joining-types fribidi-mirroring fribidi-run fribidi-shape; do
    $CC $CFLAGS -DHAVE_CONFIG_H -DFRIBIDI_BUILD -I"$B" -I"$SRC/lib" -c "$SRC/lib/$f.c" -o "$B/$f.o"
  done
  rm -f "$B/libfribidi.a"
  $AR rcs "$B/libfribidi.a" "$B"/*.o
  $RANLIB "$B/libfribidi.a"

  mkdir -p "$P/include/fribidi"
  for h in fribidi-arabic fribidi-begindecls fribidi-bidi fribidi-bidi-types fribidi-bidi-types-list fribidi-common \
           fribidi-char-sets fribidi-char-sets-list fribidi-deprecated fribidi-enddecls fribidi-flags fribidi-joining \
           fribidi-joining-types fribidi-joining-types-list fribidi-mirroring fribidi-brackets fribidi-shape \
           fribidi-types fribidi-unicode fribidi-unicode-version fribidi; do
    cp "$SRC/lib/$h.h" "$P/include/fribidi/"
  done
  cp "$B/fribidi-config.h" "$P/include/fribidi/"
  cp "$B/libfribidi.a" "$P/lib/libfribidi.a"
  echo "fribidi: done"
}

build_watt32() {
  SRC=$L/src/Watt-32-master; B=$L/build/watt32
  rm -rf "$B"; mkdir -p "$B/build/djgpp"

  # Built in a copy, leaving the sources as they came, without the "fast" packet receiver (a NASM
  # stub, only an optimisation) and without gzip: Watt-32's private zlib would replace the real one's
  # inflate functions and corrupt everything decompressed (the themes would not load).
  cp "$SRC"/src/*.c "$SRC"/src/*.h "$SRC"/src/*.S "$SRC"/src/*.inc "$B/"
  cp -R "$SRC/src/iconv" "$B/iconv"
  sed -i.bak -e 's|^\( *\)#define USE_FAST_PKT.*|\1/* USE_FAST_PKT left out */|' \
             -e 's|^\( *\)#define USE_GZIP.*|\1/* USE_GZIP left out */|' "$B/config.h"
  rm -f "$B/config.h.bak"
  printf 'const char *w32_cflags = "%s";\nconst char *w32_cc     = "i586-pc-msdosdjgpp-gcc";\n' "$CFLAGS" > "$B/build/djgpp/cflags.h"

  # sys/djgpp.err: the error numbers of DJGPP's C library, which Watt-32's own
  # build makes by running its util/errnos.c as a DOS program. Done the same
  # way: built here with the cross compiler, run under DOSBox-X.
  mkdir -p "$B/sys" "$B/errgen"
  $CC -O2 -I"$SRC/util" -I"$SRC/inc" -o "$B/errgen/ERRNOS.EXE" "$SRC/util/errnos.c" 2> "$B/errgen/errnos.log" \
    || { tail -10 "$B/errgen/errnos.log"; return 1; }
  rm -f "$B/errgen/DJGPP.ERR" "$B/errgen/SYSERR.C"
  SDL_AUDIODRIVER=dummy timeout 60 "${DOSBOX:-$(command -v dosbox-x || echo /Applications/dosbox-x.app/Contents/MacOS/dosbox-x)}" -fastlaunch \
    -c "mount c $B/errgen" -c "c:" -c "ERRNOS.EXE -e > DJGPP.ERR" -c "ERRNOS.EXE -s > SYSERR.C" -c "exit" > /dev/null 2>&1 || true
  sleep 1
  [ -s "$B/errgen/DJGPP.ERR" ] && [ -s "$B/errgen/SYSERR.C" ] \
    || { echo "watt32: could not make the error tables (is DOSBox-X installed?)"; return 1; }
  cp "$B/errgen/DJGPP.ERR" "$B/sys/djgpp.err"
  cp "$B/errgen/SYSERR.C" "$B/build/djgpp/syserr.c"

  # The objects the DJGPP build of makefile.all lists (the Windows ones left out)
  FILES="accept bind bsddbug bsdname btree chksum close connect crc dynip echo fcntl get_ai get_ip get_ni get_xbyr geteth
    gethost gethost6 getname getnet getopt getprot getput getserv gettod idna ioctl ip4_frag ip4_in ip4_out ip6_in ip6_out
    language linkaddr listen lookup loopback misc netaddr netback neterr nettime nsapaddr oldstuff packet32 pc_cbrk pcarp
    pcbootp pcbuf pcconfig pcdbug pcdhcp pcdns pcicmp pcicmp6 pcigmp pcping pcqueue pcrarp pcrecv pcsed pcstat pctcp ports
    ppp pppoe presaddr printk profile punycode receive res_comp res_data res_debu res_init res_loc res_mkqu res_quer
    res_send run select settod shutdown signal sock_dbu sock_in sock_ini sock_io sock_prn sock_scn sock_sel socket sockopt
    split stream misc_str swsvpkt syslog syslog2 tcp_fsm tcp_md5 tftp timer transmit udp_rev version
    country fsext pcpkt32 pcpkt pcintr powerpak qmsg wdpmi x32vm"
  WFLAGS="$CFLAGS -fgnu89-inline -I$B -I$SRC/inc -DWATT32_BUILD -w"
  for f in $FILES; do
    # pcpkt.c locks the memory of the packet receiver by taking the address of its first
    # function (pkt_enqueue) to that of its last (_pkt_end), so they must stay in
    # the order they are written in: the optimiser must not move functions around
    EXTRA=""
    [ "$f" = pcpkt ] && EXTRA="-fno-toplevel-reorder -fno-reorder-functions -fno-inline"
    $CC $WFLAGS $EXTRA -c "$B/$f.c" -o "$B/$f.o"
  done
  $CC $WFLAGS -c "$B/cpumodel.S" -o "$B/cpumodel.o"

  rm -f "$B/libwatt.a"
  $AR rcs "$B/libwatt.a" "$B"/*.o
  $RANLIB "$B/libwatt.a"

  # Kept apart from the other libraries: Watt-32's headers include socket.h, netdb.h and
  # others that DJGPP has no versions of, and the program takes them in only where it
  # needs them (after the compiler's own, with -idirafter)
  mkdir -p "$P/watt32/lib"
  rm -rf "$P/watt32/include"
  cp -R "$SRC/inc" "$P/watt32/include"
  cp "$B/sys/djgpp.err" "$P/watt32/include/sys/djgpp.err"   # the C library's error numbers, made above
  cp "$B/libwatt.a" "$P/watt32/lib/libwatt.a"
  echo "watt32: done"
}

build_openmpt() {
  SRC=$L/src/libopenmpt-0.8.9+release; B=$L/build/libopenmpt-0.8.9
  rm -rf "$B"; mkdir -p "$B"

  # The sources the package's Makefile lists, without the tests, compiled as its DJGPP configuration
  # does (C++17, exceptions). NDEBUG: its start-up check of _crt0_startup_flags would assert, and
  # nothing else uses it. zlib is the program's own.
  LIST=$B/sources.txt
  (cd "$SRC" && for d in src/openmpt/all src/openmpt/base src/openmpt/logging src/openmpt/random common \
        src/openmpt/fileformat_base src/openmpt/soundbase src/openmpt/soundfile_data \
        soundlib soundlib/plugins soundlib/plugins/dmo sounddsp libopenmpt; do
      ls $d/*.cpp 2>/dev/null || true
    done) > "$LIST"
  echo "include/minimp3/minimp3.c" >> "$LIST"
  echo "include/stb_vorbis/stb_vorbis.c" >> "$LIST"
  echo "libopenmpt: $(wc -l < "$LIST" | tr -d ' ') source files"

  DEFS="-DNDEBUG -DLIBOPENMPT_BUILD -DMPT_WITH_ZLIB -DMPT_WITH_MINIMP3 -DMPT_WITH_STBVORBIS -DSTB_VORBIS_NO_PULLDATA_API -DSTB_VORBIS_NO_STDIO"
  INCS="-I$SRC -I$SRC/common -I$SRC/src -I$SRC/include -I$P/include"
  CXXF="$CFLAGS -w -std=gnu++17 -fexceptions -frtti -fpermissive -fallow-store-data-races -fno-threadsafe-statics"
  CF="$CFLAGS -w -std=gnu11 -fallow-store-data-races"

  # One object per source, named by its path, eight at a time (the compile step is a small
  # script of its own: xargs on macOS limits how long a command given to it may be)
  printf '%s\n' \
    'f=$1' \
    'o="$B/$(echo "$f" | tr / _ | sed "s/\.[a-z]*$//").o"' \
    'case "$f" in' \
    '  *.cpp) "$CXX_COMPILER" $CXXF $DEFS $INCS -c "$SRC/$f" -o "$o" ;;' \
    '  *.c)   "$CC" $CF $DEFS $INCS -c "$SRC/$f" -o "$o" ;;' \
    'esac || { echo "FAILED: $f"; exit 255; }' > "$B/compile-one.sh"
  export CC CXX_COMPILER="$HOST-g++" CXXF CF DEFS INCS SRC B
  xargs -P 8 -n 1 sh "$B/compile-one.sh" < "$LIST" || { echo "libopenmpt: a file failed to compile"; return 1; }

  rm -f "$B/libopenmpt.a"
  $AR rcs "$B/libopenmpt.a" "$B"/*.o
  $RANLIB "$B/libopenmpt.a"

  mkdir -p "$P/include/libopenmpt"
  cp "$SRC"/libopenmpt/libopenmpt*.h "$SRC"/libopenmpt/libopenmpt*.hpp "$P/include/libopenmpt/"
  cp "$B/libopenmpt.a" "$P/lib/libopenmpt.a"

  # ScummVM's configure has no --with-openmpt-prefix: it finds the library through pkg-config
  # (configure-dos.sh points PKG_CONFIG_LIBDIR at this folder). The library is C++ and uses zlib.
  mkdir -p "$P/lib/pkgconfig"
  printf '%s\n' \
    "prefix=$P" \
    'libdir=${prefix}/lib' \
    'includedir=${prefix}/include' \
    '' \
    'Name: libopenmpt' \
    'Description: Tracker module player (built for DJGPP)' \
    'Version: 0.8.9' \
    'Libs: -L${libdir} -lopenmpt -lz' \
    'Cflags: -I${includedir}' > "$P/lib/pkgconfig/libopenmpt.pc"
  echo "libopenmpt: done"
}

if [ $# -eq 0 ]; then
  set -- fribidi watt32 openmpt
fi
for what in "$@"; do
  "build_$what"
done
