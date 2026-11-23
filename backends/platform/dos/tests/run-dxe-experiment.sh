#!/bin/sh
# Experiment, not part of the gates: can a C++ module loaded with DJGPP's dlopen (DXE3) do what a ScummVM
# engine plug-in does? Builds dos-programs/dxetest (a host program and MOD.DXE), runs it under DOSBox-X and
# prints what it said. Work folder: build-dos-tests/dxe.
. "$(dirname "$0")/common.sh"

dos_env || exit 1
PATH="$DJGPP_HOME/bin:$DJGPP_HOME/i586-pc-msdosdjgpp/bin:$PATH"
DJGPP="$DJGPP_HOME/i586-pc-msdosdjgpp/etc/djgpp.env"
DJDIR="$DJGPP_HOME/i586-pc-msdosdjgpp"
export PATH DJGPP DJDIR

SRCDIR=$TESTS_DIR/dos-programs/dxetest
W=${DOS_TEST_WORK:-$SRC/build-dos-tests}/dxe
mkdir -p "$W"
cd "$W" || exit 1

CXX=i586-pc-msdosdjgpp-g++
FLAGS="-O2 -std=gnu++17 -march=i486 -I$SRCDIR"

# The module, linked by the script the plug-in build uses (dxe-link.sh): unresolved symbols allowed, the host
# supplies them; only the PLUGIN_ names exported; the unwind tables registered by the object the script adds
$CXX $FLAGS -c "$SRCDIR/mod.cpp" -o mod.o || exit 1
sh "$TESTS_DIR/../dxe-link.sh" mod.o -o MOD.DXE > dxe3gen.log 2>&1 || { cat dxe3gen.log; exit 1; }
echo "MOD.DXE: $(ls -l MOD.DXE | awk '{print $5}') bytes, $(dxe3gen --show-unres MOD.DXE 2>&1 | grep -c .) lines of unresolved symbols"

# The host's export table, made from what the module needs (dxe3res), the way ScummVM's would be
dxe3res -o syms.c MOD.DXE || exit 1
echo "export table: $(grep -c 'DXE_EXPORT' syms.c) symbols"
$CXX $FLAGS -c "$SRCDIR/host.cpp" -o host.o || exit 1
i586-pc-msdosdjgpp-gcc -O2 -march=i486 -c syms.c -o syms.o || exit 1
$CXX -o HOST.EXE host.o syms.o > link.log 2>&1 || { tail -20 link.log; echo "LINK FAILED"; exit 1; }
echo "HOST.EXE: $(ls -l HOST.EXE | awk '{print $5}') bytes"

# DOSRUN.EXE sends the error output (what a DJGPP program says when it dies) to the same file as the normal output
$CXX -O2 -march=i486 -x c++ "$TESTS_DIR/dos-programs/dosrun.c" -o DOSRUN.EXE || exit 1

printf '[cpu]\ncore=normal\ncputype=pentium_mmx\ncycles=fixed 100000\n[dosbox]\nmemsize=64\n' > dxe.conf
rm -f OUT.TXT
SDL_AUDIODRIVER=dummy timeout 60 "$DOSBOX" -fastlaunch -conf dxe.conf -c "mount c $W" -c "c:" \
  -c "set DXE_LD_LIBRARY_PATH=C:\\" -c "DOSRUN.EXE HOST.EXE > OUT.TXT" -c "exit" > dosbox.log 2>&1
sleep 1
[ -f OUT.TXT ] && tr -d '\r' < OUT.TXT || echo "no output"
