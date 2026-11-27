#!/bin/sh
# Run `make test` in the host tree (ScummVM's unit tests and the DOS suites). Prints "MAKE TEST OK <n> tests"
# if it ended in OK! after at least MIN_TESTS tests (default 679). The output is in $HOST/maketest.log.
. "$(dirname "$0")/common.sh"

MIN_TESTS=${MIN_TESTS:-679}
host_path

LOG=$HOST/maketest.log
(cd "$HOST" && make -j8 test > "$LOG" 2>&1)
STATUS=$?

LINE=$(grep "Running cxxtest tests" "$LOG" | tail -1)
COUNT=$(echo "$LINE" | sed -n 's/.*(\([0-9]*\) tests).*/\1/p')

if [ $STATUS -ne 0 ]; then
  echo "MAKE TEST FAILED: make exited with status $STATUS"
  grep -v "warning:" "$LOG" | tail -25
  exit 1
fi

case "$LINE" in
  *"OK!"*) ;;
  *)
    echo "MAKE TEST FAILED: the run did not end in OK!"
    tail -25 "$LOG"
    exit 1
    ;;
esac

[ -n "$COUNT" ] || { echo "MAKE TEST FAILED: no test count in the output"; exit 1; }
if [ "$COUNT" -lt "$MIN_TESTS" ]; then
  echo "MAKE TEST FAILED: only $COUNT tests ran, wanted at least $MIN_TESTS"
  exit 1
fi

echo "MAKE TEST OK $COUNT tests"
