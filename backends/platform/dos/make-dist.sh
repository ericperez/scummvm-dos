#!/bin/sh
# Assemble the folder to copy onto the DOS machine from a finished build folder.
#
# Usage: make-dist.sh <build folder> <output folder>
#
#   SCUMMVM.EXE, CWSDPMI.EXE   the program and its DPMI host (CWSDPMI r7)
#   PLUGINS/                   the engine plug-ins (plug-in builds)
#   SCUMMREM.ZIP, GUIICONS.DAT the themes (also SCUMMCLA.ZIP, SCUMMMOD.ZIP) and the icons
#   DATA/                      engine data and TRANSLAT.DAT, under 8.3 names
#   COPYING.TXT, LICENSES/     the licences
#
# fonts-cjk.dat (36 MB, Chinese, Japanese and Korean) is left out.
set -e

BUILD=$1
OUT=$2
if [ -z "$BUILD" ] || [ -z "$OUT" ] || [ ! -f "$BUILD/scummvm.exe" ] || [ ! -f "$BUILD/config.mk" ]; then
  echo "usage: make-dist.sh <build folder with scummvm.exe> <output folder>" >&2
  exit 2
fi

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../.." && pwd)

# Is the configuration variable $1 set in the build?
defined() {
  grep -q "^$1 *[:+]*= " "$BUILD/config.mk"
}

# The 8.3 name DOS gives a file: 8 characters of the name, a dot, 3 of the extension
short() {
  echo "$1" | awk '{
    n = index($0, ".");
    if (n == 0) { base = $0; ext = "" } else { base = substr($0, 1, n - 1); ext = substr($0, n + 1); sub(/\..*/, "", ext) }
    out = toupper(substr(base, 1, 8));
    if (ext != "") out = out "." toupper(substr(ext, 1, 3));
    print out
  }'
}

mkdir -p "$OUT/DATA"
LIST=$(mktemp "${TMPDIR:-/tmp}/dosdist.XXXXXX")
trap 'rm -f "$LIST"' EXIT

# put <source file> <folder under the output> <8.3 name>
put() {
  if echo "$2/$3" | grep -qxFf "$LIST" 2>/dev/null; then
    echo "make-dist: two files would both be called $2/$3: $1" >&2
    exit 1
  fi
  echo "$2/$3" >> "$LIST"
  cp "$1" "$OUT/$2/$3"
}

cp "$BUILD/scummvm.exe" "$OUT/SCUMMVM.EXE"
echo "./SCUMMVM.EXE" >> "$LIST"

# A plug-in build (the default; DOS_STATIC=1 makes the other kind): the engines and the detection tables are DXE modules in PLUGINS, loaded one at
# a time. Their names were cut to 8 characters by the build (PLUGIN_NAME in Makefile.common).
if defined DYNAMIC_MODULES; then
  mkdir -p "$OUT/PLUGINS"
  for dxe in "$BUILD"/plugins/*.dxe; do
    put "$dxe" PLUGINS "$(short "$(basename "$dxe")")"
  done
fi

# Themes and icons keep the names the program already looks for
cp "$SRC/gui/themes/scummremastered.zip" "$OUT/SCUMMREM.ZIP"
cp "$SRC/gui/themes/scummclassic.zip" "$OUT/SCUMMCLA.ZIP"
cp "$SRC/gui/themes/scummmodern.zip" "$OUT/SCUMMMOD.ZIP"
cp "$SRC/gui/themes/gui-icons.dat" "$OUT/GUIICONS.DAT"

if defined USE_TRANSLATION; then
  put "$SRC/gui/themes/translations.dat" DATA "$(short translations.dat)"
fi

# The pages the local web server (the launcher's LAN tab) serves
if defined USE_SDL_NET; then
  put "$SRC/dists/networking/wwwroot.zip" DATA "$(short wwwroot.zip)"
fi

# The engine data files, from the same lists the normal "make install" uses:
# "ifdef ENABLE_KYRA" before a line means that engine has to be in the build
for mk in engine_data_core.mk engine_data.mk; do
  awk '/^ifdef /{ cond = $2; next } /^endif/{ cond = ""; next } /DIST_FILES_LIST[ ]*\+=/{ print (cond == "" ? "-" : cond), $3 }' \
    "$SRC/dists/engine-data/$mk" | while read -r cond path; do
    if [ "$cond" != "-" ] && ! defined "$cond"; then
      continue
    fi
    put "$SRC/$path" DATA "$(short "$(basename "$path")")"
  done
done

# The licence of ScummVM and of the libraries linked into the program (the folder deps/collect-licenses.sh makes)
cp "$SRC/COPYING" "$OUT/COPYING.TXT"
DEPS_LICENSES=${DEPS:-$SRC/build-dos-deps/prefix}/licenses
if [ -d "$DEPS_LICENSES" ]; then
  mkdir -p "$OUT/LICENSES"
  cp -R "$DEPS_LICENSES/." "$OUT/LICENSES/"
else
  echo "make-dist: warning: no $DEPS_LICENSES (run deps/collect-licenses.sh); the libraries' licences are not in the distribution" >&2
fi

# The DPMI host, unmodified from the pinned package (deps/fetch-sources.sh). Its terms require the notice in
# LICENSES/INDEX.TXT, so it is not shipped without that.
CWSDPMI=${DEPS_ROOT:-$SRC/build-dos-deps}/src/cwsdpmi/bin/CWSDPMI.EXE
[ -f "$CWSDPMI" ] || DEPS_ROOT=${DEPS_ROOT:-$SRC/build-dos-deps} sh "$HERE/deps/fetch-sources.sh" > /dev/null
if [ ! -f "$CWSDPMI" ]; then
  echo "make-dist: no $CWSDPMI (run deps/fetch-sources.sh)" >&2
  exit 1
fi
if ! grep -q "right to receive the source code" "$OUT/LICENSES/INDEX.TXT" 2>/dev/null || [ ! -f "$OUT/LICENSES/cwsdpmi/cwsdpmi.doc" ]; then
  echo "make-dist: CWSDPMI's licence and notice are not in $OUT/LICENSES (run deps/collect-licenses.sh)" >&2
  exit 1
fi
cp "$CWSDPMI" "$OUT/CWSDPMI.EXE"

echo "Assembled in $OUT:"
(cd "$OUT" && ls -l SCUMMVM.EXE | awk '{ printf "  %-14s %8.1f MB\n", "SCUMMVM.EXE", $5 / 1048576 }')
echo "  $(ls "$OUT/DATA" | wc -l | tr -d ' ') files in DATA, $(du -sk "$OUT/DATA" | awk '{ printf "%.1f", $1 / 1024 }') MB"
echo "  $(du -sk "$OUT" | awk '{ printf "%.1f", $1 / 1024 }') MB in all"
