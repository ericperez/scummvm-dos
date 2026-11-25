#!/bin/sh
# Check that the DOS builds are current: each scummvm.exe is newer than the port's sources it is made of, dist/ has
# that program (stripped, as make-dist.sh strips it), and it has the self-tests of its backend (and libopenmpt where
# an engine plays modules: OPENMPT.DXE in a plug-in build). Prints "BUILDS OK".
#
#   BUILDS   the builds to check (default: stable stable-static)
. "$(dirname "$0")/common.sh"

BUILDS=${BUILDS:-"stable stable-static"}
FAILED=0

fail() {
  echo "BUILDS FAILED: $1"
  FAILED=1
}

dos_env || exit 1
PATH="$DJGPP_HOME/bin:$PATH"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# The engines with tracker music: those whose configure.engine needs universaltracker
TRACKER_ENGINES=$(grep -l '^add_engine .*universaltracker' "$SRC"/engines/*/configure.engine \
  | sed 's|.*/engines/\([^/]*\)/configure.engine|\1|' | tr 'a-z' 'A-Z')
[ -n "$TRACKER_ENGINES" ] || fail "no engine needs universaltracker: the search found nothing"

for name in $BUILDS; do
  dir=$SRC/build-dos-$name
  exe=$dir/scummvm.exe
  if [ ! -f "$exe" ]; then
    fail "$name: no scummvm.exe"
    continue
  fi

  # The port's sources (and the headers they include, from make's dependency files) newer than what they went into:
  # the program, or openmpt.dxe for the one file linked only into it. A file this build does not compile is not counted.
  STALE=
  for dep in "$dir"/backends/platform/dos/.deps/*.d "$dir"/audio/mods/.deps/*.d; do
    [ -f "$dep" ] || continue
    target=$exe
    [ "$(basename "$dep")" = dos-openmpt-dxe.d ] && target=$dir/backends/platform/dos/openmpt.dxe
    for file in $(sed 's/[:\\]/ /g' "$dep" | tr ' ' '\n' | grep -E "^$SRC/(backends/platform/dos|audio/mods)/"); do
      [ "$file" -nt "$target" ] && STALE="$STALE $file"
    done
  done
  for mk in "$SRC/backends/platform/dos/module.mk" "$SRC/backends/platform/dos/dos.mk" "$SRC/audio/mods/module.mk"; do
    [ "$mk" -nt "$exe" ] && STALE="$STALE $mk"
  done
  [ -z "$STALE" ] || fail "$name: older than $(echo $STALE | tr ' ' '\n' | sort -u | head -1 | sed "s|$SRC/||")"

  # dist/ has this program, stripped with the strip tool configure chose
  STRIP=$(sed -n 's/^STRIP *:= *//p' "$dir/config.mk" | head -1)
  if "$STRIP" --strip-all -o "$TMP/scummvm.exe" "$exe" 2> /dev/null; then
    cmp -s "$TMP/scummvm.exe" "$dir/dist/SCUMMVM.EXE" || fail "$name: dist/SCUMMVM.EXE is not this build's program"
  else
    fail "$name: cannot strip scummvm.exe with '$STRIP'"
  fi

  # The self-test hooks and messages only a build of the current sources has (the keyboard and CD hooks are the
  # port's own backend's)
  MARKERS="SCUMMVM_DOS_SHOTS SCUMMVM_DOS_QUITAT"
  if grep -q "^BACKEND := sdl" "$dir/config.mk"; then
    BACKEND=SDL3
  else
    BACKEND="own backend"
    MARKERS="$MARKERS SCUMMVM_DOS_KBDTEST SCUMMVM_DOS_CDTEST"
  fi
  for marker in $MARKERS "MSCDEX is not loaded"; do
    grep -a -q "$marker" "$exe" || fail "$name: no '$marker' in the program"
  done

  # Tracker music: wherever an engine plays modules, libopenmpt is in
  SIZE=$(ls -l "$exe" | awk '{ printf "%.1f MB", $5 / 1048576 }')
  TRACKER=
  for engine in $TRACKER_ENGINES; do
    grep -q "^ENABLE_$engine = " "$dir/config.mk" && TRACKER="$TRACKER $engine"
  done
  if [ -n "$TRACKER" ]; then
    grep -q "^#define USE_OPENMPT" "$dir/config.h" || fail "$name: has a tracker engine ($TRACKER) but not libopenmpt (config.h)"
    grep -q "lopenmpt" "$dir/config.mk" || fail "$name: libopenmpt is not linked"
    grep -a -q "Parsing mod error" "$exe" || fail "$name: the tracker player is not in the program"
    # A plug-in build has libopenmpt in OPENMPT.DXE, next to the program
    if grep -q "^DYNAMIC_MODULES" "$dir/config.mk"; then
      [ -f "$dir/dist/OPENMPT.DXE" ] || fail "$name: no dist/OPENMPT.DXE"
      cmp -s "$dir/backends/platform/dos/openmpt.dxe" "$dir/dist/OPENMPT.DXE" 2> /dev/null \
        || fail "$name: dist/OPENMPT.DXE is not this build's openmpt.dxe"
      echo "  $name ($BACKEND): current, with libopenmpt in OPENMPT.DXE ($SIZE)"
    else
      echo "  $name ($BACKEND): current, with libopenmpt ($SIZE)"
    fi
  else
    grep -q "^#define USE_OPENMPT" "$dir/config.h" && fail "$name: libopenmpt is in a build with no engine that plays modules"
    echo "  $name ($BACKEND): current, no engine plays modules so no libopenmpt ($SIZE)"
  fi
done

[ "$FAILED" -eq 0 ] && echo "BUILDS OK"
[ "$FAILED" -eq 0 ]
