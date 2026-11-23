#!/bin/sh
# Cross-compile SDL3 (with its DOS driver) for 32-bit MS-DOS with DJGPP, targeting the 486, as a static
# library. Only the SDL3 backend of the port needs it (configure-dos.sh with DOS_BACKEND=sdl).
#
#   sources   $L/src/SDL3    (fetch-sources.sh)
#   objects   $L/build/SDL3
#   installed $L/prefix/{include,lib}, with lib/pkgconfig/sdl3.pc for ScummVM's configure
#
# Needs cmake (3.16 or later) and ninja or make on PATH, besides the DJGPP cross-compiler.
# Usage: build-sdl3.sh
set -e

DEPS_DIR=$(cd "$(dirname "$0")" && pwd)
DOS_DIR=$(cd "$DEPS_DIR/.." && pwd)
. "$DOS_DIR/dos-env.sh"
dos_env || exit 1
L=${DEPS_ROOT:-$SRC/build-dos-deps}

command -v cmake > /dev/null 2>&1 || { echo "error: cmake not found" >&2; exit 1; }
[ -d "$L/src/SDL3" ] || sh "$DEPS_DIR/fetch-sources.sh"

if command -v ninja > /dev/null 2>&1; then GENERATOR=Ninja; else GENERATOR="Unix Makefiles"; fi

rm -rf "$L/build/SDL3"
cmake -S "$L/src/SDL3" -B "$L/build/SDL3" -G "$GENERATOR" \
  -DCMAKE_TOOLCHAIN_FILE="$L/src/SDL3/build-scripts/i586-pc-msdosdjgpp.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-march=i486 -mtune=i486" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
  -DCMAKE_INSTALL_PREFIX="$L/prefix"
cmake --build "$L/build/SDL3"
cmake --install "$L/build/SDL3"
echo "SDL3 is in $L/prefix"
