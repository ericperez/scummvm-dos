#!/bin/sh
# Tracker music in a plug-in build is OPENMPT.DXE (PLUGINS.md). Check that
#  - the engines the program loads it for (dosEngineUsesTracker() in dos-openmpt.h) are the engines whose
#    configure.engine needs universaltracker, no more and no fewer
#  - in each plug-in build given, the program holds no libopenmpt code (namespace OpenMPT), openmpt.dxe exports
#    only its entry point, and dist/ has OPENMPT.DXE next to SCUMMVM.EXE, not in PLUGINS
#
# Usage: check-openmpt-dxe.sh [build folder ...]
# Prints "OPENMPT DXE OK".
. "$(dirname "$0")/common.sh"

fail() { echo "OPENMPT DXE FAILED: $1"; exit 1; }

IN_CODE=$(sed -n 's/.*static const char \*const engines\[\] = { \(.*\) };.*/\1/p' "$DOS_DIR/dos-openmpt.h" | tr ',' '\n' | tr -d '" ' | sort | tr '\n' ' ')
[ -n "$IN_CODE" ] || fail "no engine list in dos-openmpt.h"
NEEDED=$(grep -l '^add_engine .*universaltracker' "$SRC"/engines/*/configure.engine | sed 's|.*/engines/\([^/]*\)/configure.engine|\1|' | sort | tr '\n' ' ')
[ -n "$NEEDED" ] || fail "no engine needs universaltracker: the search found nothing"
[ "$IN_CODE" = "$NEEDED" ] || fail "dos-openmpt.h loads OPENMPT.DXE for: $IN_CODE; the engines with tracker music are: $NEEDED"

if [ $# -gt 0 ]; then
  dos_env || exit 1
  DJDIR=$DJGPP_HOME/i586-pc-msdosdjgpp
  export DJDIR
fi
for build in "$@"; do
  name=$(basename "$build")
  grep -q "^DYNAMIC_MODULES" "$build/config.mk" || fail "$name is not a plug-in build"
  grep -q "^#define USE_OPENMPT" "$build/config.h" || fail "$name has no tracker music"
  "$DJDIR/bin/nm" "$build/scummvm.exe" | grep -q "N7OpenMPT" && fail "$name: libopenmpt code is in scummvm.exe"
  DXE=$build/backends/platform/dos/openmpt.dxe
  [ -f "$DXE" ] || fail "$name: no openmpt.dxe"
  # (from its own folder: dxe3gen lists nothing for a long path)
  EXPORTS=$(cd "$(dirname "$DXE")" && PATH="$DJDIR/bin:$PATH" dxe3gen --show-exp openmpt.dxe | tr -d '\r' | tr '\n' ' ')
  [ "$EXPORTS" = "_dosOpenMPTGetApi " ] || fail "$name: openmpt.dxe exports '$EXPORTS'"
  [ -f "$build/dist/OPENMPT.DXE" ] || fail "$name: no dist/OPENMPT.DXE"
  cmp -s "$DXE" "$build/dist/OPENMPT.DXE" || fail "$name: dist/OPENMPT.DXE is not this build's"
  ls "$build/dist/PLUGINS" | grep -qi '^openmpt' && fail "$name: OPENMPT.DXE is in PLUGINS"
  echo "  $name: libopenmpt in OPENMPT.DXE ($(ls -l "$DXE" | awk '{ printf "%.1f MB", $5 / 1048576 }')), not in the program"
done

echo "OPENMPT DXE OK (engines: $(echo $NEEDED))"
