#!/bin/sh
# Links a plug-in's objects into a DXE3 module; configure makes it the linker for plugins/*.dxe:
#
#   dxe-link.sh [-E <export prefix>] <object files> [linker options] -o <plugins/name.dxe>
#
# Unresolved symbols are allowed (-U, the program's export table supplies them), only the plug-in entry
# points are exported (-E _PLUGIN_, or the prefix given), and dos-plugin-eh.cpp is added to register the
# unwind tables. Linker options (-L, -l) go to the link as they are.
set -e

OUT=
OBJS=
OPTIONS=
EXPORT=_PLUGIN_
while [ $# -gt 0 ]; do
  case "$1" in
    -o) OUT=$2; shift 2 ;;
    -E) EXPORT=$2; shift 2 ;;
    -*) OPTIONS="$OPTIONS $1"; shift ;;
    *) OBJS="$OBJS $1"; shift ;;
  esac
done
[ -n "$OUT" ] || { echo "dxe-link.sh: no -o <module> given" >&2; exit 2; }

HERE=$(cd "$(dirname "$0")" && pwd)
EH_SOURCE=$HERE/../../plugins/dos/dos-plugin-eh.cpp

# The cross compiler's folders: from the environment, else from where the compiler is
if [ -z "$DJDIR" ]; then
  GXX=$(command -v i586-pc-msdosdjgpp-g++) || { echo "dxe-link.sh: i586-pc-msdosdjgpp-g++ is not on the PATH" >&2; exit 2; }
  DJDIR=$(cd "$(dirname "$GXX")/.." && pwd)/i586-pc-msdosdjgpp
fi
export DJDIR
PATH="$DJDIR/bin:$PATH"
export PATH

# dxe3gen writes dxe_tmp.o in the current folder: each module gets its own (make -j), with absolute names
HERE_BUILD=$(pwd)
absolute() {
  case "$1" in
    /*) echo "$1" ;;
    *) echo "$HERE_BUILD/$1" ;;
  esac
}
OUT=$(absolute "$OUT")
# dxe3gen crashes on a very long command line (ultima has some 700 objects), so the object
# names go through a short link to the build folder
SHORT=$(mktemp -d /tmp/dxe.XXXXXX)
trap 'rm -rf "$SHORT"' EXIT
ln -s "$HERE_BUILD" "$SHORT/b"
ABS_OBJS=
for obj in $OBJS; do
  case "$obj" in
    /*) ABS_OBJS="$ABS_OBJS $obj" ;;
    *) ABS_OBJS="$ABS_OBJS $SHORT/b/$obj" ;;
  esac
done

WORK=$(dirname "$OUT")/.dxe-work/$(basename "${OUT%.dxe}")
mkdir -p "$WORK"
cd "$WORK"

# The registration object (not a plug-in: it does not end in .dxe)
EH_OBJECT=$SHORT/eh.o
i586-pc-msdosdjgpp-g++ -O2 -march=i486 -fno-rtti -c "$EH_SOURCE" -o "$EH_OBJECT"

# shellcheck disable=SC2086
dxe3gen -o "$OUT" -U -E "$EXPORT" $ABS_OBJS "$EH_OBJECT" $OPTIONS
