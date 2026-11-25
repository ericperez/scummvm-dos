#!/bin/sh
# Reconfigure and rebuild the DOS builds from the current sources, one after the other (two makes at
# once would fight over the machine), and assemble each one's dist/ folder.
#
# Usage: rebuild-builds.sh [name ...]      the names of the test builds (configure_named_build in common.sh),
#                                          each in build-dos-<name> in the source folder (default:
#                                          stable stable-static)
# Each build's logs are in its folder: configure.out and build-rebuild.log.
. "$(dirname "$0")/common.sh"

dos_env || exit 1

[ $# -gt 0 ] || set -- stable stable-static

# Configure again only if it never was, or something configure reads is newer (configure, the DOS scripts, the
# engine lists, common.sh); otherwise make rebuilds only what changed. FORCE_CONFIGURE=1 configures anyway.
needs_configure() {
  [ -n "$FORCE_CONFIGURE" ] && return 0
  [ -f config.mk ] && [ -f config.h ] || return 0
  # configure leaves config.mk and config.h alone when they come out the same, so configure.stamp says when it last ran
  stamp=configure.stamp
  [ -f "$stamp" ] || stamp=config.mk
  for f in "$SRC/configure" "$DOS_DIR/configure-dos.sh" "$DOS_DIR/dos-env.sh" "$DOS_DIR/engines.conf" "$TESTS_DIR/common.sh"; do
    [ "$f" -nt "$stamp" ] && return 0
  done
  return 1
}

# Is dist/ made from the program and plug-ins as they are now? (make-dist.sh copies 20 to 130 MB.)
dist_current() {
  [ -f dist/SCUMMVM.EXE ] || return 1
  for f in scummvm.exe plugins/*.dxe backends/platform/dos/openmpt.dxe "$SRC/backends/platform/dos/make-dist.sh"; do
    [ -f "$f" ] && [ "$f" -nt dist/SCUMMVM.EXE ] && return 1
  done
  return 0
}

for name in "$@"; do
  dir=$(build_dir "$name")
  mkdir -p "$dir" && cd "$dir" || exit 1

  if needs_configure; then
    # Which kind of build a name is (engines inside the program, or plug-ins) is part of its recipe in common.sh
    date "+%H:%M:%S configuring $name"
    configure_named_build "$name" > configure.out 2>&1 || { echo "configure failed for $name"; tail -8 configure.out; exit 1; }
  fi

  date "+%H:%M:%S building $name"
  make -j8 > build-rebuild.log 2>&1 || { echo "make failed for $name"; grep -n "error:\|\*\*\* " build-rebuild.log | head; exit 1; }

  if dist_current; then
    :
  else
    sh "$SRC/backends/platform/dos/make-dist.sh" "$dir" "$dir/dist" > dist.out 2>&1 || { echo "make-dist failed for $name"; tail -5 dist.out; exit 1; }
  fi

  date "+%H:%M:%S done $name"
  ls -l "$dir/scummvm.exe" | awk '{ printf "  %.1f MB\n", $5 / 1048576 }'
done
echo "REBUILD FINISHED: $*"
