#!/bin/sh
# Fetch and cross-build every library the DOS port links, into build-dos-deps/prefix (the folder
# dos-env.sh uses for DEPS when it is not set), and gather their licence texts.
#
# Needs the DJGPP cross-compiler (see ../dos-env.sh). Watt-32 also needs DOSBox-X, to make its error tables.
# Usage: build-all.sh
set -e
DEPS_DIR=$(cd "$(dirname "$0")" && pwd)
sh "$DEPS_DIR/fetch-sources.sh"
sh "$DEPS_DIR/build-libs.sh"
sh "$DEPS_DIR/build-image-libs.sh"
sh "$DEPS_DIR/build-audio-libs.sh"
sh "$DEPS_DIR/build-extra-libs.sh"
sh "$DEPS_DIR/collect-licenses.sh"
echo "DOS libraries are ready."
