#!/bin/sh
# Check the plug-in build (build-dos-stable):
#  - plugins/ has a DXE for each engine and the detection tables, with unique 8.3 names, and dist/PLUGINS has them
#  - the program holds none of the engines (the static build does: the control) and is smaller by about their size
#  - the program's export table covers every symbol the plug-ins leave unresolved (and the same check on a table
#    with entries missing must find them missing: the control)
# Prints "PLUGIN BUILD OK".
. "$(dirname "$0")/common.sh"

dos_env || exit 1
PATH="$DJGPP_HOME/bin:$DJGPP_HOME/i586-pc-msdosdjgpp/bin:$PATH"
DJDIR="$DJGPP_HOME/i586-pc-msdosdjgpp"
export PATH DJDIR

BUILD=${PLUGINS_BUILD:-$SRC/build-dos-stable}
STATIC=${STATIC_BUILD:-$SRC/build-dos-stable-static}
WORK=${DOS_TEST_WORK:-$SRC/build-dos-tests}/plugin-build-check
mkdir -p "$WORK"

fail() { echo "PLUGIN BUILD FAILED: $1"; exit 1; }

[ -f "$BUILD/scummvm.exe" ] || fail "no $BUILD/scummvm.exe"
cd "$BUILD" || exit 1

# ---- the files ----
for e in agi groovie mohawk sci scumm detectio; do
  [ -f "plugins/$e.dxe" ] || fail "plugins/$e.dxe was not built"
  [ -f "dist/PLUGINS/$(echo "$e" | tr 'a-z' 'A-Z').DXE" ] || fail "dist/PLUGINS lacks $(echo "$e" | tr 'a-z' 'A-Z').DXE (run make-dist.sh)"
done
COUNT=$(ls plugins/*.dxe | wc -l | tr -d ' ')
[ "$COUNT" -ge 6 ] || fail "expected at least 6 plug-ins, found $COUNT"
for f in plugins/*.dxe; do
  base=$(basename "$f" .dxe)
  [ "${#base}" -le 8 ] || fail "$f has a name longer than 8 characters"
done
DUP=$(ls plugins/*.dxe | tr 'A-Z' 'a-z' | sort | uniq -d)
[ -z "$DUP" ] || fail "two plug-ins have the same name: $DUP"
# each is a DXE3 module
for f in plugins/*.dxe; do
  [ "$(head -c 4 "$f")" = "DXE1" ] || fail "$f is not a DXE module"
done

# ---- the program holds no engine ----
ENGINE_IN_PROGRAM=$(i586-pc-msdosdjgpp-nm scummvm.exe 2>/dev/null | grep -c "ScummEngine\|AgiEngine\|GroovieEngine")
ENGINE_IN_STATIC=$(i586-pc-msdosdjgpp-nm "$STATIC/scummvm.exe" 2>/dev/null | grep -c "ScummEngine\|AgiEngine\|GroovieEngine")
[ "$ENGINE_IN_STATIC" -gt 100 ] || fail "control: the static program should hold the engines ($ENGINE_IN_STATIC symbols)"
[ "$ENGINE_IN_PROGRAM" -eq 0 ] || fail "the plug-in build's program still holds engine code ($ENGINE_IN_PROGRAM symbols)"
dxe3gen --show-exp plugins/scumm.dxe | grep -q "_PLUGIN_getObject" || fail "scumm.dxe does not export its plug-in entry point"

SIZE_PROGRAM=$(ls -l scummvm.exe | awk '{ print $5 }')
SIZE_STATIC=$(ls -l "$STATIC/scummvm.exe" | awk '{ print $5 }')
SIZE_PLUGINS=$(ls -l plugins/*.dxe | awk '{ s += $5 } END { print s }')
SAVED=$((SIZE_STATIC - SIZE_PROGRAM))
# The program is smaller by about what the plug-ins hold (the table, the loader and the symbols the plug-ins pull in
# cost a little): at least 70% of it
[ "$SAVED" -ge $((SIZE_PLUGINS * 7 / 10)) ] || fail "the program is only $((SAVED / 1024)) KB smaller; the plug-ins are $((SIZE_PLUGINS / 1024)) KB"

# ---- the export table covers every unresolved symbol ----
: > "$WORK/unresolved.txt"
: > "$WORK/provided-by-modules.txt"
for f in plugins/*.dxe; do
  dxe3gen --show-unres "$f" | sed 's/^[[:space:]]*//' | grep . >> "$WORK/unresolved.txt"
  dxe3gen --show-exp "$f" | sed 's/^[[:space:]]*//' | grep . >> "$WORK/provided-by-modules.txt"
done
sort -u "$WORK/unresolved.txt" > "$WORK/unresolved.sorted"
sort -u "$WORK/provided-by-modules.txt" > "$WORK/provided.sorted"
# what the program has to supply
comm -23 "$WORK/unresolved.sorted" "$WORK/provided.sorted" > "$WORK/needed.sorted"
NEEDED=$(wc -l < "$WORK/needed.sorted" | tr -d ' ')
[ "$NEEDED" -ge 100 ] || fail "suspiciously few symbols needed from the program ($NEEDED)"

# the table's symbols, as extern_asm(<name>) lines
sed -n 's/^extern_asm(\(.*\));$/\1/p' backends/platform/dos/dos-exports.c | sort -u > "$WORK/table.sorted"
TABLE=$(wc -l < "$WORK/table.sorted" | tr -d ' ')
MISSING=$(comm -23 "$WORK/needed.sorted" "$WORK/table.sorted")
[ -z "$MISSING" ] || fail "the export table lacks: $(echo "$MISSING" | head -5 | tr '\n' ' ')"

# control: with 5 entries left out of the table the same comparison must find them missing
tail -n +6 "$WORK/table.sorted" > "$WORK/table-short.sorted"
CONTROL=$(comm -23 "$WORK/needed.sorted" "$WORK/table-short.sorted" | wc -l | tr -d ' ')
[ "$CONTROL" -ge 1 ] || fail "control: a table with entries removed should be found short"

echo "PLUGIN BUILD OK ($COUNT plug-ins, $((SIZE_PLUGINS / 1024)) KB; program $((SIZE_PROGRAM / 1024)) KB, $((SAVED / 1024)) KB smaller than the static build; $TABLE symbols exported, $NEEDED needed)"
