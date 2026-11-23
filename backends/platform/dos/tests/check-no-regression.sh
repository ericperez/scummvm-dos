#!/bin/sh
# Nothing that worked before the plug-in build is broken: every ScummVM unit test (make test), the static
# stable build (stable-static) is current with the sources, and every DOS runtime test passes under DOSBox-X
# (keyboard, CD audio, network, plug-ins, ...).
# Prints "NO REGRESSION".
. "$(dirname "$0")/common.sh"

fail() { echo "NO REGRESSION FAILED: $1"; exit 1; }

OUT=$(sh "$TESTS_DIR/host-make-test.sh" 2>&1) || fail "make test: $(echo "$OUT" | tail -3)"
echo "$OUT" | grep -q "MAKE TEST OK" || fail "make test did not pass"
TESTS=$(echo "$OUT" | grep "MAKE TEST OK" | sed 's/MAKE TEST OK //')

OUT=$(BUILDS="stable-static" sh "$TESTS_DIR/check-builds.sh" 2>&1) || fail "the static builds are not current: $(echo "$OUT" | tail -3)"

OUT=$(sh "$TESTS_DIR/run-dos-tests.sh" 2>&1) || fail "DOS runtime tests: $(echo "$OUT" | tail -5)"
echo "$OUT" | grep -q "ALL DOS TESTS OK" || fail "the DOS runtime tests did not all pass"
RUNTIME=$(echo "$OUT" | grep "ALL DOS TESTS OK" | sed 's/ALL DOS TESTS OK //')

echo "NO REGRESSION (make test: $TESTS; DOS tests: $RUNTIME)"
