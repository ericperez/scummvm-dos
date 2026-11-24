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
#
# The output folder is erased first: it is generated, and nothing in it is kept. Only the plug-ins this configuration
# enables are copied (config.mk), not every .dxe an earlier configuration left in the build folder.
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

# The program is stripped of its symbol and debug tables (about 4 MB, read by no one on the DOS machine). The build
# folder keeps the unstripped scummvm.exe for debugging. The strip tool is the one configure chose (STRIP in config.mk).
STRIP=$(sed -n 's/^STRIP *:= *//p' "$BUILD/config.mk" | head -1)
if [ -z "$STRIP" ] || ! command -v "$STRIP" > /dev/null 2>&1; then
  echo "make-dist: no strip tool ($STRIP) on PATH; put the DJGPP bin folder on PATH" >&2
  exit 1
fi

# The plug-ins this configuration builds: the engines it marks DYNAMIC_PLUGIN in config.mk, each cut to 8 characters
# as the build names it (PLUGIN_NAME), and the detection tables. Checked before anything is erased.
EXPECTED_PLUGINS=
if defined DYNAMIC_MODULES; then
  DETECTION=$(grep -o 'PLUGIN_DETECTION_NAME=\\"[a-z0-9_]*' "$BUILD/config.mk" | head -1 | sed 's/.*=\\"//')
  [ -n "$DETECTION" ] || { echo "make-dist: no PLUGIN_DETECTION_NAME in $BUILD/config.mk" >&2; exit 1; }
  EXPECTED_PLUGINS="$DETECTION $(grep ' = DYNAMIC_PLUGIN$' "$BUILD/config.mk" | sed 's/^ENABLE_\([A-Za-z0-9_]*\) = .*/\1/' | tr 'A-Z' 'a-z' | cut -c1-8 | tr '\n' ' ')"
  for id in $EXPECTED_PLUGINS; do
    [ -f "$BUILD/plugins/$id.dxe" ] || { echo "make-dist: $id.dxe is enabled in $BUILD but was not built (run make -C $BUILD)" >&2; exit 1; }
  done
fi

# The output folder is erased before it is filled. It must be a folder of its own: never the root, a home folder, the
# current folder, the source tree or the build folder, nor a folder that holds any of them.
if [ -d "$OUT" ]; then
  OUT_ABS=$(cd "$OUT" && pwd)
  BUILD_ABS=$(cd "$BUILD" && pwd)
  refuse() { echo "make-dist: not erasing $OUT_ABS: $1" >&2; exit 2; }
  [ "$OUT_ABS" != / ] || refuse "it is the root"
  [ "$OUT_ABS" != "${HOME:-/nonexistent}" ] || refuse "it is a home folder"
  [ "$OUT_ABS" != "$PWD" ] || refuse "it is the current folder"
  [ "$OUT_ABS" != "$SRC" ] || refuse "it is the source tree"
  [ "$OUT_ABS" != "$BUILD_ABS" ] || refuse "it is the build folder"
  case "$PWD/" in "$OUT_ABS"/*) refuse "it holds the current folder" ;; esac
  case "$SRC/" in "$OUT_ABS"/*) refuse "it holds the source tree" ;; esac
  case "$BUILD_ABS/" in "$OUT_ABS"/*) refuse "it holds the build folder" ;; esac
  case "$OUT_ABS/" in
    "$SRC"/build-*/*) ;;
    "$SRC"/*) refuse "it is inside the source tree, outside a build folder" ;;
  esac
  rm -rf "$OUT"
fi

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

"$STRIP" --strip-all -o "$OUT/SCUMMVM.EXE" "$BUILD/scummvm.exe"
echo "./SCUMMVM.EXE" >> "$LIST"

# A plug-in build (the default; DOS_STATIC=1 makes the other kind): the engines and the detection tables are DXE modules in PLUGINS, loaded one at
# a time. Their names were cut to 8 characters by the build (PLUGIN_NAME in Makefile.common).
if defined DYNAMIC_MODULES; then
  mkdir -p "$OUT/PLUGINS"
  for id in $EXPECTED_PLUGINS; do
    put "$BUILD/plugins/$id.dxe" PLUGINS "$(short "$id.dxe")"
  done
  for dxe in "$BUILD"/plugins/*.dxe; do
    id=$(basename "$dxe" .dxe)
    case " $EXPECTED_PLUGINS " in
      *" $id "*) ;;
      *) echo "make-dist: not copying $(basename "$dxe"): not enabled in this build" ;;
    esac
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
