#!/bin/sh
# Check that the native (host) build tree the unit tests run in exists and is a native build of this
# source: configured here, not for DOS, with the null backend (no screen needed), and able to run
# the test generator. Prints "HOST TREE OK" when it is.
. "$(dirname "$0")/common.sh"

fail() { echo "HOST TREE FAILED: $1"; exit 1; }

[ -d "$HOST" ] || fail "no host tree at $HOST (configure a native build there: see README.md in this folder)"
[ -f "$HOST/config.mk" ] && [ -f "$HOST/Makefile" ] || fail "$HOST is not configured"

# It must build this source tree
SRCDIR=$(sed -n 's/^srcdir *:\{0,1\}= *//p' "$HOST/Makefile" | head -1)
[ -n "$SRCDIR" ] || SRCDIR=$(sed -n 's/^srcdir *:\{0,1\}= *//p' "$HOST/config.mk" | head -1)
[ -n "$SRCDIR" ] && [ "$(cd "$HOST" && cd "$SRCDIR" 2>/dev/null && pwd)" = "$SRC" ] || fail "the host tree was configured for another source folder ($SRCDIR)"

# And be native: a DJGPP configuration would build programs the Mac cannot run
grep -q "msdosdjgpp" "$HOST/config.mk" && fail "the host tree is configured for DOS, not for this machine"
grep -q "USE_DOS_DRIVER" "$HOST/config.h" && fail "the host tree has the DOS backend"

command -v python3 > /dev/null || fail "python3 is missing (the test generator needs it)"
command -v make > /dev/null || fail "make is missing"
host_path
command -v python > /dev/null || fail "no 'python' command for the test generator"

BACKEND=$(grep -o -e '--backend=[a-z0-9]*' "$HOST/config.mk" | head -1)
echo "HOST TREE OK ($HOST, ${BACKEND:-default backend})"
