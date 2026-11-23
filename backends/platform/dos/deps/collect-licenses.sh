#!/bin/sh
# Gather the licence texts of the linked libraries and of CWSDPMI into build-dos-deps/prefix/licenses
# (make-dist.sh copies it to LICENSES), with an index of each package's licence and source.
#
# Usage: collect-licenses.sh
set -e

DEPS_DIR=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$DEPS_DIR/../../../.." && pwd)
L=${DEPS_ROOT:-$SRC/build-dos-deps}
OUT=$L/prefix/licenses
rm -rf "$OUT"
mkdir -p "$OUT"
INDEX=$OUT/INDEX.TXT
{
  echo "Libraries linked into this ScummVM build, and CWSDPMI.EXE shipped with it, their licences and where they come from."
  echo "The licence texts are in the folders next to this file. ScummVM itself is GPL-3.0-or-later (COPYING)."
  echo "For the GPL-licensed libraries the corresponding source is the package named below."
  echo
} > "$INDEX"

grep -v '^[[:space:]]*#' "$DEPS_DIR/sources.conf" | grep . | while read -r name licence file sum url; do
  [ -d "$L/src/$name" ] || { echo "error: $L/src/$name is missing; run fetch-sources.sh" >&2; exit 1; }
  dest=$OUT/$(echo "$name" | tr -c 'A-Za-z0-9\n' '_')
  mkdir -p "$dest"
  found=0
  for f in "$L/src/$name"/COPYING* "$L/src/$name"/LICENSE* "$L/src/$name"/LICENCE* "$L/src/$name"/COPYRIGHT* \
           "$L/src/$name"/docs/LICENSE* "$L/src/$name"/docs/FTL* "$L/src/$name"/docs/GPLv2* \
           "$L/src/$name"/inc/copying.* "$L/src/$name"/bin/cwsdpmi.doc; do
    [ -f "$f" ] && cp "$f" "$dest/" && found=1
  done
  if [ $found = 0 ] && [ "$name" = jpeg ] && [ -f "$L/src/$name/README" ]; then
    cp "$L/src/$name/README" "$dest/README"; found=1   # the IJG terms are in its README
  fi
  [ $found = 1 ] || echo "warning: no licence file found for $name" >&2
  printf '%-26s %-20s %s\n' "$name" "$licence" "$url" >> "$INDEX"
done

# CWSDPMI's terms (cwsdpmi.doc) ask for this notice when it is redistributed without its source
if grep -q '^cwsdpmi ' "$DEPS_DIR/sources.conf"; then
  {
    echo
    echo "CWSDPMI.EXE is Copyright (C) 1995-2010 Charles W Sandmann, shipped unmodified (terms in cwsdpmi/cwsdpmi.doc)."
    echo "You have the right to receive the source code and binary updates for CWSDPMI, from"
    echo "https://sandmann.dotster.com/cwsdpmi/ (source: csdpmi7s.zip)."
  } >> "$INDEX"
fi
echo "Licences are in $OUT"
