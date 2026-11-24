#!/bin/sh
# Cross-configure ScummVM for 32-bit MS-DOS (DJGPP), out of tree. The one profile, stable, is every
# finished engine a DOS machine can run (engines.conf). Engines are DXE plug-ins by default (PLUGINS.md).
#
# Usage:  configure-dos.sh stable [build-folder]
#
#   backends/platform/dos/configure-dos.sh stable
#   make -C build-dos-stable -j8
#
# The default build folder is build-dos-stable in the source folder (-static and -sdl are added for
# DOS_STATIC=1 and DOS_BACKEND=sdl). ENGINES_ONLY and EXTRA_ENGINES need a folder of their own.
#
# Environment:
#   EXTRA_ENGINES=...     engines to add, e.g. "testbed"
#   ENGINES_ONLY=...      only these engines, e.g. "scumm" (sub-engines by name: scumm_7_8, he)
#   DOS_NO_NETWORK=1      leave out the local web server (Watt-32)
#   DOS_NO_TRANSLATION=1  leave out the translated user interface
#   DOS_BACKEND=sdl       SDL3's DOS driver for video, input and PCM (needs deps/build-sdl3.sh)
#   DOS_STATIC=1          engines inside the program instead of plug-ins
#   DOS_NO_OPENMPT=1      leave out tracker music (libopenmpt)
#   DOS_MT32EMU=1         add the software MT-32 synthesiser (Pentium II or faster)
#   DJGPP_HOME, DEPS      the cross compiler and the cross-built libraries (see dos-env.sh)

PROFILE=$1
case "$PROFILE" in
  stable) ;;
  *) echo "usage: configure-dos.sh stable [build-folder]" >&2; exit 2 ;;
esac

DOS_DIR=$(cd "$(dirname "$0")" && pwd)
. "$DOS_DIR/dos-env.sh"
. "$DOS_DIR/engines.conf"

BUILD=$2
if [ -z "$BUILD" ]; then
  if [ -n "$ENGINES_ONLY$EXTRA_ENGINES" ]; then
    echo "error: ENGINES_ONLY and EXTRA_ENGINES change what the $PROFILE profile builds, so the build needs a folder of its own." >&2
    echo "Give the build folder as the second argument, e.g. configure-dos.sh $PROFILE $SRC/build-dos-myengines" >&2
    exit 2
  fi
  BUILD=$SRC/build-dos-$PROFILE${DOS_STATIC:+-static}
  [ "$DOS_BACKEND" = sdl ] && BUILD=$BUILD-sdl
fi

dos_env || exit 1
dos_check_libs || exit 1

case "$DOS_BACKEND" in
  ""|dos) BACKEND_OPTION= ;;   # configure chooses the port's own backend for DJGPP
  sdl)
    [ -f "$DEPS/lib/libSDL3.a" ] && [ -f "$DEPS/lib/pkgconfig/sdl3.pc" ] || {
      echo "error: DOS_BACKEND=sdl needs SDL3 built for DJGPP in $DEPS (sh $DOS_DIR/deps/build-sdl3.sh)." >&2
      exit 1
    }
    # The Dear ImGui debugger would come with SDL: too heavy for a DOS machine. SDL's DOS driver has no OpenGL, so
    # the OpenGL graphics manager would only be tried and fail, and offered in the Graphics options all the same.
    BACKEND_OPTION="--backend=sdl --opengl-mode=none --disable-imgui"
    ;;
  *) echo "error: DOS_BACKEND is dos or sdl" >&2; exit 2 ;;
esac
echo "Backend: ${BACKEND_OPTION:---backend=dos (default)}"

mkdir -p "$BUILD" && BUILD=$(cd "$BUILD" && pwd) || exit 1
cd "$BUILD" || exit 1
CONFIGURE=$SRC/configure
echo "Build folder: $BUILD"

# configure asks pkg-config about some libraries (FreeType); left alone it would
# answer with the Mac's own. Point it at the DOS libraries and nothing else.
PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig"
PKG_CONFIG_PATH=
export PKG_CONFIG_LIBDIR PKG_CONFIG_PATH

CXXFLAGS="-march=i486 -mtune=i486"
export CXXFLAGS

case "$PROFILE" in
  stable) ENGINES="$DOS_ENGINES" ;;
esac
ENGINES="$ENGINES $EXTRA_ENGINES"
# A list of your own replaces the profile's: only these engines are built
if [ -n "$ENGINES_ONLY" ]; then
  ENGINES="$ENGINES_ONLY"
fi
ENGINE_LIST=$(echo $ENGINES | tr ' ' ',')

# optlib <configure name> <file name pattern in $DEPS/lib>
# Use the library if it has been built for DJGPP, otherwise switch it off.
optlib() {
  if ls "$DEPS"/lib/$2 > /dev/null 2>&1; then
    echo "--with-$1-prefix=$DEPS"
  else
    echo "--disable-$1"
  fi
}

echo "Profile: $PROFILE, engines: $(echo $ENGINES | wc -w)"

# Engines and the detection tables as DXE plug-ins, unless DOS_STATIC=1 (PLUGINS.md)
if [ -n "$DOS_STATIC" ]; then
  PLUGIN_OPTIONS=
else
  PLUGIN_OPTIONS="--enable-plugins --default-dynamic --enable-detection-dynamic"
fi
echo "Plug-ins: ${PLUGIN_OPTIONS:-none (DOS_STATIC: the engines are in the program)}"

OPTIONAL_LIBS="$(optlib zlib 'libz.a') $(optlib png 'libpng*.a') $(optlib jpeg 'libjpeg*.a') \
$(optlib gif 'libgif*.a') $(optlib freetype2 'libfreetype*.a') $(optlib mpeg2 'libmpeg2*.a') \
$(optlib theoradec 'libtheoradec*.a') \
$(optlib a52 'liba52.a') $(optlib faad 'libfaad.a') $(optlib mpcdec 'libmpcdec.a') \
$(optlib fribidi 'libfribidi.a')"
echo "Libraries: $OPTIONAL_LIBS"

# Tracker music (MOD, S3M, XM, IT...) through libopenmpt, unless DOS_NO_OPENMPT=1
if [ -z "$DOS_NO_OPENMPT" ] && [ -f "$DEPS/lib/libopenmpt.a" ] && [ -f "$DEPS/lib/pkgconfig/libopenmpt.pc" ]; then
  TRACKER=--enable-openmpt
else
  TRACKER=--disable-openmpt
fi
echo "Tracker music: $TRACKER"

# The local web server over Watt-32, unless DOS_NO_NETWORK=1
if [ -z "$DOS_NO_NETWORK" ] && [ -f "$DEPS/watt32/lib/libwatt.a" ]; then
  NETWORK="--with-sdlnet-prefix=$DEPS/watt32"
else
  NETWORK=--disable-sdlnet
fi
echo "Network: $NETWORK"

# The user interface in the language of the CONFIG.SYS country (translations.dat
# next to the program). DOS_NO_TRANSLATION=1 leaves it out.
if [ -n "$DOS_NO_TRANSLATION" ]; then
  TRANSLATION=--disable-translation
else
  TRANSLATION=--enable-translation
fi

# The software MT-32 synthesiser needs a Pentium II or faster: only with DOS_MT32EMU=1
if [ -n "$DOS_MT32EMU" ]; then
  MT32EMU=--enable-mt32emu
else
  MT32EMU=--disable-mt32emu
fi


exec "$CONFIGURE" \
  --host=i586-pc-msdosdjgpp $BACKEND_OPTION \
  --disable-all-engines --enable-engine="$ENGINE_LIST" \
  --disable-detection-full \
  --disable-ext-sse2 --disable-ext-avx2 \
  --enable-release --disable-debug \
  $PLUGIN_OPTIONS \
  $OPTIONAL_LIBS \
  --disable-vorbis --enable-tremor --enable-ogg --enable-flac --enable-mad \
  --with-ogg-prefix=$DEPS --with-tremor-prefix=$DEPS --with-flac-prefix=$DEPS --with-mad-prefix=$DEPS \
  --disable-mikmod $TRACKER \
  --disable-vpx \
  --disable-fluidsynth --disable-fluidlite --disable-sonivox $MT32EMU \
  --disable-nuked-opl --disable-seq-midi --disable-timidity \
  --disable-alsa --disable-sndio \
  --disable-opengl-game --disable-opengl-game-classic --disable-opengl-game-shaders --enable-tinygl \
  --disable-nasm --disable-readline --disable-libunity --disable-gtk \
  --disable-libcurl $NETWORK --disable-cloud --disable-enet \
  --disable-discord --disable-tts $TRANSLATION --disable-taskbar \
  --disable-system-dialogs --disable-sparkle --disable-osx-dock-plugin
