#!/bin/sh
# Build CODEC.EXE (codectest.c) against the cross-built libraries. Usage: build-codectest.sh <output.exe>
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DOS_DIR=$(cd "$HERE/../.." && pwd)
. "$DOS_DIR/dos-env.sh"
dos_env || exit 1
L=${DEPS_ROOT:-$SRC/build-dos-deps}
i586-pc-msdosdjgpp-gcc -O2 -march=i486 -mtune=i486 -I"$L/prefix/include" -o "$1" "$HERE/codectest.c" \
  -L"$L/prefix/lib" -lFLAC -lvorbisidec -logg -lmad -lm
