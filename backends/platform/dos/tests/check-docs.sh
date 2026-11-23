#!/bin/sh
# Check that the notes describe the new features and not the old state: FEATURES.md says what the
# keyboard, tracker music and CD audio now do (and no longer says they are missing), the tests README
# lists every runtime test and every unit suite there is, and the library notes cover libopenmpt.
# Prints "DOCS OK".
. "$(dirname "$0")/common.sh"

DOSDIR=$SRC/backends/platform/dos
FEATURES=$DOSDIR/FEATURES.md
README=$DOSDIR/tests/README.md
LIBS=$DOSDIR/deps/README.md
FAILED=0

fail() {
  echo "DOCS FAILED: $1"
  FAILED=1
}

has() { # <file> <text>
  grep -q -F -- "$2" "$1" || fail "$(basename "$1") lacks: $2"
}

hasnt() { # <file> <text that must be gone>
  grep -q -F -- "$2" "$1" && fail "$(basename "$1") still says: $2"
}

[ -f "$FEATURES" ] || fail "no FEATURES.md"
[ -f "$README" ] || fail "no tests/README.md"
[ -f "$LIBS" ] || fail "no $LIBS"

# What the notes say now
has "$FEATURES" "KEYB"
has "$FEATURES" "libopenmpt 0.8.9"
has "$FEATURES" "MSCDEX"
has "$FEATURES" "tests/README.md"
has "$FEATURES" "universaltracker.cpp"
[ -z "$LIBS" ] || has "$LIBS" "libopenmpt"
[ -z "$LIBS" ] || has "$LIBS" "libopenmpt.pc"
has "$DOSDIR/configure-dos.sh" "DOS_NO_OPENMPT"

# Plug-ins
PLUGINS_DOC=$DOSDIR/PLUGINS.md
ENGINES=$DOSDIR/ENGINES.md
[ -f "$PLUGINS_DOC" ] || fail "no PLUGINS.md"
for text in "DOS_STATIC=1" "dxe-link.sh" "dxe-exports.sh" "isAllDetectionLoaded" "PLUGIN_NAME_MAX" "check-plugin-ram.sh" "110 MB" "32 MB" "SCUMMVM_DOS_PLUGINTEST" "Rebuilding after a change" "FORCE_CONFIGURE"; do
  has "$PLUGINS_DOC" "$text"
done
has "$FEATURES" "PLUGINS.md"
has "$ENGINES" "PLUGINS.md"
has "$DOSDIR/configure-dos.sh" "DOS_STATIC"
hasnt "$PLUGINS_DOC" "opt-in"
hasnt "$FEATURES" "new, opt-in"

# What they must no longer say
hasnt "$FEATURES" "US layout | same |"
hasnt "$FEATURES" "DOS's KEYB layouts are ignored"
hasnt "$FEATURES" "left out. MOD, XM and S3M play through"
hasnt "$FEATURES" "Real CD audio through MSCDEX (other platforms"
hasnt "$FEATURES" "Other keyboard layouts: use the ASCII"
hasnt "$FEATURES" "not done, but possible"
hasnt "$ENGINES" "does not use plug-ins"
hasnt "$ENGINES" "no DOS plugin loader"
hasnt "$ENGINES" "That is an estimate from the sizes above"

# The README lists every runtime test case of run-dos-test.sh and every unit suite
for name in $(sed -n 's/^  \([a-z]*\))$/\1/p' "$DOSDIR/tests/run-dos-test.sh"); do
  grep -q -F -- "| \`$name\` |" "$README" || fail "tests/README.md does not list the runtime test '$name'"
done
for suite in $(sed -n 's/^class \(Dos[A-Za-z]*TestSuite\).*/\1/p' "$SRC"/test/backends/dos/*.h); do
  grep -q -F -- "$suite" "$README" || fail "tests/README.md does not list the suite $suite"
done

# Every script in tests/ is mentioned somewhere in its README
for script in "$DOSDIR"/tests/*.sh; do
  base=$(basename "$script")
  [ "$base" = common.sh ] && continue
  grep -q -F -- "$base" "$README" || fail "tests/README.md does not mention $base"
done

[ "$FAILED" -eq 0 ] && echo "DOCS OK"
[ "$FAILED" -eq 0 ]
