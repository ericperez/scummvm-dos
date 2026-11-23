#!/bin/sh
# Makes the program's export table: the symbols the plug-ins use from the program, which dlopen()
# resolves them from (and which keeps the linker from dropping that code).
#
#   dxe-exports.sh <dos-exports.c> <plugins/*.dxe>
#
# dxe3res writes the table as C. An unchanged table is not rewritten, so the program is not relinked for nothing.
set -e

OUT=$1
shift
[ -n "$OUT" ] && [ $# -gt 0 ] || { echo "usage: dxe-exports.sh <output.c> <module.dxe> ..." >&2; exit 2; }

if [ -z "$DJDIR" ]; then
  GXX=$(command -v i586-pc-msdosdjgpp-g++) || { echo "dxe-exports.sh: i586-pc-msdosdjgpp-g++ is not on the PATH" >&2; exit 2; }
  DJDIR=$(cd "$(dirname "$GXX")/.." && pwd)/i586-pc-msdosdjgpp
fi
export DJDIR
PATH="$DJDIR/bin:$PATH"
export PATH

# Sorted, so the same modules always make the same table
# shellcheck disable=SC2046
set -- $(printf '%s\n' "$@" | LC_ALL=C sort)

dxe3res -o "$OUT.new" "$@"
if [ -f "$OUT" ] && cmp -s "$OUT" "$OUT.new"; then
  :
else
  mv "$OUT.new" "$OUT"
fi
