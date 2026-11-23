#!/bin/sh
# The RAM the program holds at its launcher must not depend on the number of engines. Measured under DOSBox-X
# with 128 MB (nothing swapped out): the free memory DOSRUN.EXE reports before the start, less the program's
# at its first event.
#
#  - the all-engines plug-in build must hold at most 32 MB
#  - the all-engines static build must hold at least 60 MB (the control: it holds about 97)
#
# Prints "PLUGIN RAM OK" and the figures.
#
#   STABLE_PLUGINS, STABLE_STATIC   the build folders (default: next to the source)
. "$(dirname "$0")/common.sh"

dos_env || exit 1
WORK=${DOS_TEST_WORK:-$SRC/build-dos-tests}/ram
STABLE_PLUGINS=${STABLE_PLUGINS:-$SRC/build-dos-stable}
STABLE_STATIC=${STABLE_STATIC:-$SRC/build-dos-stable-static}
mkdir -p "$WORK"

fail() { echo "PLUGIN RAM FAILED: $1"; exit 1; }

[ -x "$DOSBOX" ] || fail "no DOSBox-X at $DOSBOX"
for b in "$STABLE_PLUGINS" "$STABLE_STATIC"; do
  [ -f "$b/dist/SCUMMVM.EXE" ] || fail "no $b/dist/SCUMMVM.EXE (build it, then make-dist.sh)"
done

PATH="$DJGPP_HOME/bin:$PATH" DJGPP="$DJGPP_HOME/i586-pc-msdosdjgpp/etc/djgpp.env" \
  "$DJGPP_HOME/bin/i586-pc-msdosdjgpp-g++" -O2 -march=i486 -x c++ "$TESTS_DIR/dos-programs/dosrun.c" -o "$WORK/DOSRUN.EXE" \
  || fail "cannot build DOSRUN.EXE"

# held <build folder>: the MB the program holds at its launcher, or a failure
held() {
  build=$1
  run=$WORK/run-$(basename "$build")
  mkdir -p "$run"
  find "$run" -type f -delete 2>/dev/null
  cp -R "$build/dist/." "$run/"
  cp "$WORK/DOSRUN.EXE" "$run/"
  printf '[cpu]\ncore=normal\ncputype=pentium_mmx\ncycles=fixed 100000\n[dosbox]\nmemsize=128\n' > "$WORK/ram.conf"
  SDL_AUDIODRIVER=dummy timeout 300 "$DOSBOX" -fastlaunch -conf "$WORK/ram.conf" -c "mount c $run" -c "c:" \
    -c "set SCUMMVM_DOS_TESTEXIT=1" -c "DOSRUN.EXE SCUMMVM.EXE > OUT.TXT" -c "exit" > "$WORK/dosbox-ram.log" 2>&1
  sleep 1
  tr -d '\r' < "$run/OUT.TXT" 2>/dev/null | grep -q "DOSRUN: exit status 0" || { echo "did not end cleanly"; return 1; }
  before=$(tr -d '\r' < "$run/OUT.TXT" | sed -n 's/^DOSRUN: free memory at start: \([0-9]*\) KB.*/\1/p' | head -1)
  after=$(grep "the program is running (its first event)" "$run/SCUMMVM.LOG" 2>/dev/null | sed -n 's/.*free memory \([0-9]*\) KB.*/\1/p' | head -1)
  [ -n "$before" ] && [ -n "$after" ] || { echo "no memory figures"; return 1; }
  echo $(( (before - after) / 1024 ))
}

STABLE_P=$(held "$STABLE_PLUGINS") || fail "stable plug-in build: $STABLE_P"
STABLE_S=$(held "$STABLE_STATIC") || fail "stable static build: $STABLE_S"

[ "$STABLE_P" -le 32 ] || fail "the all-engines plug-in build holds $STABLE_P MB at its launcher: more than 32"
[ "$STABLE_S" -ge 60 ] || fail "control: the all-engines static build holds only $STABLE_S MB, so this measures nothing"

echo "PLUGIN RAM OK (held at the launcher: all engines as plug-ins $STABLE_P MB, all engines static $STABLE_S MB)"
