#!/bin/sh
# Check that configure sets up the DOS plug-in build (build-dos-stable: .dxe modules, 8.3 names, the uncached
# plug-in manager, dos.mk and the link script) and leaves the static build's config.h and config.mk unchanged
# (build-dos-stable-static). Make both with rebuild-builds.sh stable stable-static.
#
# Prints "PLUGIN CONFIG OK".
. "$(dirname "$0")/common.sh"

PLUGINS_BUILD=$(build_dir stable)
STATIC_BUILD=$(build_dir stable-static)
mkdir -p "$WORK"

fail() { echo "PLUGIN CONFIG FAILED: $1"; exit 1; }

has() { # <file> <fixed text>
  grep -q -F -- "$2" "$1" || fail "$(basename "$PWD")/$(basename "$1") lacks: $2"
}

[ -f "$PLUGINS_BUILD/config.h" ] || fail "no plug-in build at $PLUGINS_BUILD (make it with: sh rebuild-builds.sh stable)"
cd "$PLUGINS_BUILD" || exit 1

has config.h "#define DYNAMIC_MODULES"
has config.h '#define PLUGIN_SUFFIX	".dxe"'
has config.mk "DYNAMIC_MODULES = 1"
has config.mk "PLUGIN_SUFFIX := .dxe"
has config.mk "-DUNCACHED_PLUGINS"
has config.mk "-DPLUGIN_NAME_MAX=8"
has config.mk 'PLUGIN_DETECTION_NAME=\"detectio\"'
has config.mk "PLUGIN_NAME = "
grep -q "PLUGIN_DETECTION_NAME" "$SRC/base/plugins.cpp" || fail "base/plugins.cpp does not use PLUGIN_DETECTION_NAME: the detection plug-in (detectio.dxe) would not be found"
has config.mk "plugins/%.dxe: LD := sh "
has config.mk "port_mk = backends/platform/dos/dos.mk"
for e in AGI GROOVIE MOHAWK SCI SCUMM; do
  has config.mk "ENABLE_$e = DYNAMIC_PLUGIN"
done
grep -q "^#define DETECTION_STATIC\|^DETECTION_STATIC" config.h config.mk && fail "the detection tables are static: they should be a plug-in"

# The static build (DOS_STATIC=1) must not notice: configure again, and compare
[ -f "$STATIC_BUILD/config.h" ] || fail "no static build at $STATIC_BUILD"
cd "$STATIC_BUILD" || exit 1
cp config.h "$WORK/static-config.h.before"
cp config.mk "$WORK/static-config.mk.before"
configure_named_build stable-static > "$WORK/static-configure.out" 2>&1 || fail "configure of the static build fails"
cmp -s config.h "$WORK/static-config.h.before" || fail "config.h of the static build changed"
cmp -s config.mk "$WORK/static-config.mk.before" || fail "config.mk of the static build changed"
grep -q "^#define DYNAMIC_MODULES\|^DYNAMIC_MODULES *=\|^port_mk *= .*dos.mk\|^#define PLUGIN_SUFFIX" config.h config.mk && fail "the static build got plug-in settings"

echo "PLUGIN CONFIG OK (plug-in build configured; static build's config.h and config.mk unchanged)"
