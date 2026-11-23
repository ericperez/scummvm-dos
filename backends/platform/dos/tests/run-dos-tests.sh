#!/bin/sh
# Run every DOS runtime test (run-dos-test.sh) under DOSBox-X, one after another, and say whether
# they all passed. Prints "ALL DOS TESTS OK" if so, otherwise the output of the ones that failed.
#
#   DOS_BUILD   the build folder whose dist/ is run (default: build-dos-stable)
#   TESTS       the tests to run, if not all (default: nodpmi kbd cd nocd net smoke)
HERE=$(cd "$(dirname "$0")" && pwd)
TESTS=${TESTS:-"nodpmi kbd cd nocd net smoke"}

PASSED=0
FAILED=0
for name in $TESTS; do
  OUT=$(sh "$HERE/run-dos-test.sh" "$name" 2>&1)
  echo "$OUT" | head -1
  case "$OUT" in
    "DOSTEST $name OK"*) PASSED=$((PASSED + 1)) ;;
    *)
      FAILED=$((FAILED + 1))
      echo "$OUT" | tail -15
      ;;
  esac
done

if [ "$FAILED" -eq 0 ]; then
  echo "ALL DOS TESTS OK ($PASSED tests)"
else
  echo "DOS TESTS FAILED: $FAILED of $((PASSED + FAILED))"
  exit 1
fi
