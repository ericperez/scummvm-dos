#!/bin/sh
# Measurement, not a gate: package the engines of a finished build (their object files, as they are linked into
# SCUMMVM.EXE) as DXE modules and report each module's size and what it needs from the main program.
# Usage: measure-dxe-engines.sh [build name] [engine ...]     (default: stable-static agi groovie mohawk sci scumm)
. "$(dirname "$0")/common.sh"

dos_env || exit 1
PATH="$DJGPP_HOME/bin:$DJGPP_HOME/i586-pc-msdosdjgpp/bin:$PATH"
DJGPP="$DJGPP_HOME/i586-pc-msdosdjgpp/etc/djgpp.env"
DJDIR="$DJGPP_HOME/i586-pc-msdosdjgpp"
export PATH DJGPP DJDIR

BUILD=${1:-stable-static}
[ $# -gt 0 ] && shift
[ $# -gt 0 ] || set -- agi groovie mohawk sci scumm
B=$SRC/build-dos-$BUILD
W=${DOS_TEST_WORK:-$SRC/build-dos-tests}/dxe/real-$BUILD
mkdir -p "$W"
cd "$W" || exit 1
: > all.unres

for e in "$@"; do
  upper=$(echo "$e" | tr 'a-z' 'A-Z')
  objs=$(find "$B/engines/$e" -name '*.o' | sort)
  count=$(echo "$objs" | wc -l | tr -d ' ')
  # shellcheck disable=SC2086
  if dxe3gen -o "$upper.DXE" -U $objs > "$e.log" 2>&1; then
    size=$(ls -l "$upper.DXE" | awk '{printf "%.2f", $5 / 1048576}')
    dxe3gen --show-unres "$upper.DXE" > "$e.unres" 2>&1
    syms=$(grep -c . "$e.unres")
    cat "$e.unres" >> all.unres
    echo "$e: $count objects, module $size MB, needs $syms symbols from the program"
  else
    echo "$e: dxe3gen failed"
    tail -3 "$e.log"
  fi
done

echo "together: $(sort -u all.unres | grep -c .) different symbols"
