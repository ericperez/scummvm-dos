#!/bin/sh
# Check that the DOS builds are current: each scummvm.exe is newer than the port's sources, dist/ has that
# program, and it has the self-tests (and libopenmpt where an engine plays modules). Prints "BUILDS OK".
#
#   BUILDS   the builds to check (default: stable stable-static)
. "$(dirname "$0")/common.sh"

BUILDS=${BUILDS:-"stable stable-static"}
FAILED=0

fail() {
  echo "BUILDS FAILED: $1"
  FAILED=1
}

for name in $BUILDS; do
  dir=$SRC/build-dos-$name
  exe=$dir/scummvm.exe
  if [ ! -f "$exe" ]; then
    fail "$name: no scummvm.exe"
    continue
  fi

  # Sources the program is made of (not the tests, tools and notes next to them)
  STALE=$(find "$SRC/backends/platform/dos" "$SRC/audio/mods" \
    \( -path '*/tests' -o -path '*/tools' \) -prune -o \
    -type f \( -name '*.cpp' -o -name '*.h' -o -name 'module.mk' \) \
    -newer "$exe" -print | head -3)
  [ -z "$STALE" ] || fail "$name: older than $(echo "$STALE" | head -1 | sed "s|$SRC/||")"

  cmp -s "$exe" "$dir/dist/SCUMMVM.EXE" || fail "$name: dist/SCUMMVM.EXE is not this build's program"

  # The self-test hooks and messages only a build of the current sources has
  for marker in SCUMMVM_DOS_KBDTEST SCUMMVM_DOS_CDTEST "MSCDEX is not loaded"; do
    grep -a -q "$marker" "$exe" || fail "$name: no '$marker' in the program"
  done

  # Tracker music: wherever an engine plays modules (AGS, SLUDGE), libopenmpt is in
  SIZE=$(ls -l "$exe" | awk '{ printf "%.1f MB", $5 / 1048576 }')
  if grep -q "enable-engine=.*\(ags\|sludge\)" "$dir/config.mk"; then
    grep -q "^#define USE_OPENMPT" "$dir/config.h" || fail "$name: has a tracker engine but not libopenmpt (config.h)"
    grep -q "lopenmpt" "$dir/config.mk" || fail "$name: libopenmpt is not linked"
    grep -a -q "Parsing mod error" "$exe" || fail "$name: the tracker player is not in the program"
    echo "  $name: current, with libopenmpt ($SIZE)"
  else
    grep -q "^#define USE_OPENMPT" "$dir/config.h" && fail "$name: libopenmpt is in a build with no engine that plays modules"
    echo "  $name: current, no engine plays modules so no libopenmpt ($SIZE)"
  fi
done

[ "$FAILED" -eq 0 ] && echo "BUILDS OK"
[ "$FAILED" -eq 0 ]
