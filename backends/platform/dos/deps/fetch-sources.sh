#!/bin/sh
# Download the packages listed in sources.conf, check each against its sha256 and unpack it, untouched,
# into build-dos-deps/src/<name>. Packages already there are left alone.
#
# Usage: fetch-sources.sh            (DEPS_ROOT=<folder> to use another folder than build-dos-deps)
set -e

DEPS_DIR=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$DEPS_DIR/../../../.." && pwd)
L=${DEPS_ROOT:-$SRC/build-dos-deps}
mkdir -p "$L/dl" "$L/src"

sha256() {
  if command -v sha256sum > /dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

grep -v '^[[:space:]]*#' "$DEPS_DIR/sources.conf" | grep . | while read -r name licence file sum url; do
  if [ -d "$L/src/$name" ]; then
    continue
  fi
  if [ ! -f "$L/dl/$file" ]; then
    echo "fetching $file"
    curl -fL --retry 3 -o "$L/dl/$file.part" "$url"
    mv "$L/dl/$file.part" "$L/dl/$file"
  fi
  if [ "$(sha256 "$L/dl/$file")" != "$sum" ]; then
    echo "error: $file does not match the checksum in sources.conf (expected $sum)." >&2
    echo "The download is not the package this port was tested with; see deps/README.md." >&2
    exit 1
  fi
  # Unpack into a scratch folder first: the packages' top folders have their own names
  tmp=$(mktemp -d "$L/src/.unpack.XXXXXX")
  case "$file" in
    *.zip)
      # DOS zips (CWSDPMI) have no folder around their contents: the scratch folder becomes the package's folder
      unzip -q "$L/dl/$file" -d "$tmp"
      mv "$tmp" "$L/src/$name"
      continue
      ;;
  esac
  tar -xf "$L/dl/$file" -C "$tmp"
  top=$(ls "$tmp")
  [ "$(echo "$top" | wc -l | tr -d ' ')" = 1 ] || { echo "error: $file has more than one top folder" >&2; exit 1; }
  mv "$tmp/$top" "$L/src/$name"
  rmdir "$tmp"
done
echo "Sources are in $L/src"
