# Where the DOS build finds its tools; sourced by configure-dos.sh and tests/, with DOS_DIR set to this folder.
#
#   DJGPP_HOME   DJGPP's cross-compiler folder (not needed when it is on PATH)
#   DEPS         the cross-built libraries, Watt-32 in DEPS/watt32
#                (default: build-dos-deps/prefix, made by deps/build-all.sh)
#   DJGPP        the cross-compiler's djgpp.env (default: the one in its tree)
#
# dos_env exports PATH, DJGPP_HOME, DJGPP and DEPS, and sets SRC to the source folder.

DOS_TARGET=i586-pc-msdosdjgpp
SRC=$(cd "$DOS_DIR/../../.." && pwd)

dos_env() {
  if [ -n "$DJGPP_HOME" ]; then
    PATH="$DJGPP_HOME/bin:$PATH"
  fi
  DOS_GXX=$(command -v "$DOS_TARGET-g++") || {
    echo "error: $DOS_TARGET-g++ not found." >&2
    echo "Install DJGPP's cross-compiler and put its bin folder on PATH, or set DJGPP_HOME to the folder it is installed in." >&2
    return 1
  }
  if [ -z "$DJGPP_HOME" ]; then
    DJGPP_HOME=$(cd "$(dirname "$DOS_GXX")/.." && pwd)
  fi
  if [ -z "$DJGPP" ] && [ -f "$DJGPP_HOME/$DOS_TARGET/etc/djgpp.env" ]; then
    DJGPP="$DJGPP_HOME/$DOS_TARGET/etc/djgpp.env"
  fi
  DEPS=${DEPS:-$SRC/build-dos-deps/prefix}
  export PATH DJGPP_HOME DEPS
  [ -z "$DJGPP" ] || export DJGPP
}

# The libraries every profile needs: Ogg, Tremor (Ogg Vorbis), FLAC and MAD (MP3). Say what is missing and how to
# say where they are, instead of leaving it to configure to fail further on.
dos_check_libs() {
  missing=
  for lib in libogg.a libvorbisidec.a libFLAC.a libmad.a; do
    [ -f "$DEPS/lib/$lib" ] || missing="$missing $lib"
  done
  if [ -n "$missing" ]; then
    echo "error: DEPS=$DEPS has no:$missing" >&2
    echo "Build them with: sh $DOS_DIR/deps/build-all.sh   (or set DEPS to a prefix they were cross-built into)." >&2
    return 1
  fi
}
