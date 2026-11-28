#!/bin/sh
# Check that the DOS port's pure logic has unit tests, and that they are part of `make test`: each
# suite below exists as a file under test/backends/dos, is picked up by the test list in test/module.mk, is
# known to the built runner with at least the number of tests given, and passes. Prints "DOS SUITES OK".
. "$(dirname "$0")/common.sh"

fail() { echo "DOS SUITES FAILED: $1"; exit 1; }

# <test file> <suite> <least tests> <the DOS header it tests>
SUITES="
keymap.h   DosKeymapTestSuite   15 dos-keymap.h
cd.h       DosCdTestSuite       20 dos-cd.h
dither.h   DosDitherTestSuite   15 dos-dither.h
modes.h    DosModesTestSuite    22 dos-modes.h
names.h    DosNamesTestSuite     6 dos-names.h
netparse.h DosNetParseTestSuite 10 dos-netparse.h
joystick.h DosJoystickTestSuite 10 dos-joystick.h
filesys.h  DosFileSysTestSuite  10 dos-filesys.h
banner.h   DosBannerTestSuite   10 dos-banner.h
scancodes.h DosScanCodesTestSuite 20 dos-scancodes.h
blaster.h  DosBlasterTestSuite  12 dos-blaster.h
vesa.h     DosVesaTestSuite     12 dos-vesainfo.h
dmaclock.h DosDmaClockTestSuite 20 dos-dmaclock.h
"

grep -q 'test/backends/dos/\*\.h' "$SRC/test/module.mk" || fail "test/module.mk does not list test/backends/dos/*.h"

build_runner || fail "the test runner does not build"
ALL=$(cd "$HOST" && ./test/runner --help-tests 2>&1)

echo "$SUITES" | while read -r file suite least header; do
  [ -n "$file" ] || continue
  [ -f "$SRC/test/backends/dos/$file" ] || { echo "DOS SUITES FAILED: test/backends/dos/$file is missing"; exit 1; }
  [ -f "$SRC/backends/platform/dos/$header" ] || { echo "DOS SUITES FAILED: $header is missing"; exit 1; }
  grep -q "include.*$header" "$SRC/test/backends/dos/$file" || { echo "DOS SUITES FAILED: $file does not test $header"; exit 1; }
done || exit 1

echo "$SUITES" | {
  TOTAL=0
  SUITE_COUNT=0
  while read -r file suite least header; do
    [ -n "$file" ] || continue
    COUNT=$(echo "$ALL" | grep -c "^$suite ")
    [ "$COUNT" -ge "$least" ] || { echo "DOS SUITES FAILED: $suite has $COUNT tests, wanted $least"; exit 1; }
    OUT=$(cd "$HOST" && ./test/runner "$suite" 2>&1)
    case "$OUT" in
      *"OK!"*) ;;
      *) echo "DOS SUITES FAILED: $suite does not pass"; echo "$OUT" | tail -8; exit 1 ;;
    esac
    TOTAL=$((TOTAL + COUNT))
    SUITE_COUNT=$((SUITE_COUNT + 1))
  done
  echo "DOS SUITES OK ($TOTAL tests in $SUITE_COUNT suites)"
}
