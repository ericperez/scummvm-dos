#!/bin/sh
# Run one unit test suite from the host tree's runner and check it has at least N tests.
# Usage: host-suite.sh <SuiteName> <minimum tests>
# Prints "SUITE OK <SuiteName> tests=<n>" when the suite ran and every test in it passed.
. "$(dirname "$0")/common.sh"

SUITE=$1
MIN=${2:-1}
[ -n "$SUITE" ] || { echo "usage: host-suite.sh <SuiteName> <minimum tests>"; exit 2; }

build_runner || exit 1

COUNT=$(cd "$HOST" && ./test/runner --help-tests 2>&1 | grep -c "^$SUITE ")
if [ "$COUNT" -lt "$MIN" ]; then
  echo "only $COUNT tests in $SUITE, wanted at least $MIN"
  exit 1
fi

OUT=$(cd "$HOST" && ./test/runner "$SUITE" 2>&1)
echo "$OUT" | tail -4
case "$OUT" in
  *"OK!"*) echo "SUITE OK $SUITE tests=$COUNT" ;;
  *) echo "SUITE FAILED $SUITE"; exit 1 ;;
esac
