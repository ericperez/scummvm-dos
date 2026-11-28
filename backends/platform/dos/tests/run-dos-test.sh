#!/bin/sh
# Run one of the DOS program's own tests under DOSBox-X: the checks that need a DOS machine (BIOS, MSCDEX,
# VESA, a packet driver, DXE loading). None needs a game. Usage: run-dos-test.sh <name>
#
#   kbd             layout characters reach the decoder (German layout, KEYB)
#   cd              CD audio tracks of a CD image play and stop through MSCDEX, with no rip-the-tracks dialog
#   nocd            without a CD drive the program says so and carries on
#   net             the local web server answers a browser (emulated network card)
#   pageflip        page flipping at 640x480x8: the display matches the picture every frame
#   pageflipbanked  the same with banked access
#   pageflip16      the same in 800x600 high colour
#   plugadd         --add finds a folder holding an empty TESTBED file, through the detection plug-in
#   plugsmoke       the Testbed engine runs through its plug-in and is left cleanly
#   mpt             a MOD, an S3M, an XM and an IT file play with sound through ScummVM's tracker stream (libopenmpt)
#   mptdxe          tracker music is OPENMPT.DXE: not loaded for the launcher, loaded for a Testbed game, plays a
#                   MOD file, unloaded with the engine's plug-in (mptdxesdl: the same on the SDL3 build)
#   smoke           the launcher comes up and draws
#   nodpmi          without dist's CWSDPMI.EXE the program cannot start (so the other tests ran on it)
#   sdl             the SDL3 build: VESA video, Sound Blaster audio, the launcher draws, a clean exit
#   scalesdl        the SDL3 build: a 3x scale factor on a card with nothing above 800x600: the launcher stays at 1x
#                   and the game is lowered to 2x, rather than set for a screen larger than every video mode
#   nomouse         no mouse driver: one warning in the log, a reminder on leaving, a clean exit (nomousesdl: the same
#                   on the SDL3 build)
#
# Prints "DOSTEST <name> OK". Environment:
#   DOS_BUILD       the build whose dist/ is run (default: build-dos-stable)
#   TESTBED_BUILD   for plugadd, plugsmoke, mpt, mptdxe and nomouse (default: build-dos-testbed)
#   SDL_BUILD       for sdl, scalesdl, mptdxesdl and nomousesdl (default: build-dos-sdl-testbed)
#   KEEP=1          keep the run folder afterwards
. "$(dirname "$0")/common.sh"

NAME=$1
[ -n "$NAME" ] || { echo "usage: run-dos-test.sh <name>"; exit 2; }

DOS_BUILD=${DOS_BUILD:-$SRC/build-dos-stable}
# The plug-in tests run the plug-in build (engines and detection tables as DXE modules in PLUGINS)
case "$NAME" in
  # These need no game data: the Testbed engine (build-dos-testbed, made with ENGINES_ONLY=testbed) is detected by
  # an empty file called TESTBED
  plugadd|plugsmoke|mpt|mptdxe|nomouse) DOS_BUILD=${TESTBED_BUILD:-$SRC/build-dos-testbed} ;;
  sdl|scalesdl|mptdxesdl|nomousesdl) DOS_BUILD=${SDL_BUILD:-$SRC/build-dos-sdl-testbed} ;;
  plug*|pageflip*) DOS_BUILD=${PLUGIN_BUILD:-$SRC/build-dos-stable} ;;
esac
WORK=${DOS_TEST_WORK:-$SRC/build-dos-tests}
RUN=$WORK/run-$NAME

[ -f "$DOS_BUILD/dist/SCUMMVM.EXE" ] || { echo "no $DOS_BUILD/dist/SCUMMVM.EXE: build and make-dist first"; exit 1; }
[ -f "$DOS_BUILD/dist/CWSDPMI.EXE" ] || { echo "no $DOS_BUILD/dist/CWSDPMI.EXE: run make-dist again"; exit 1; }
[ -x "$DOSBOX" ] || { echo "no DOSBox-X at $DOSBOX"; exit 1; }

# A clean run folder holding a copy of the distribution
prepare() {
  mkdir -p "$WORK"
  if [ -d "$RUN" ]; then
    find "$RUN" -type f -delete 2>/dev/null
  fi
  mkdir -p "$RUN"
  cp -R "$DOS_BUILD/dist/." "$RUN/"
}

# A DOSBox-X configuration: [cpu]/[dosbox] settings, plus whatever sections $3 adds
write_conf() { # <file> <memsize> <extra sections>
  printf '[cpu]\ncore=normal\ncputype=pentium_mmx\ncycles=fixed 100000\n[dosbox]\nmemsize=%s\n%s' "$2" "$3" > "$1"
}

# DOSBox-X has a CWSDPMI.EXE of its own in Z:\BIN, on its default PATH, which a DJGPP program would start when it
# finds none next to itself. The tests leave Z:\BIN out, so the program runs on the dist's CWSDPMI.EXE, as on a real
# DOS machine (the nodpmi test checks that nothing else is left).
DOS_PATH='Z:\;Z:\SYSTEM'

# run_dosbox <conf> <seconds> <dosbox -c arguments...>: quiet, no sound output, bounded in time
run_dosbox() {
  CONF=$1; SECS=$2; shift 2
  SDL_AUDIODRIVER=dummy timeout "$SECS" "$DOSBOX" -fastlaunch -conf "$CONF" -c "set PATH=$DOS_PATH" "$@" > "$WORK/dosbox-$NAME.log" 2>&1
  sleep 1
}

# The log the program wrote, without the noise
program_log() {
  # (the SDL3 build writes scummvm.log; the same file on a case-insensitive disk)
  cat "$RUN"/[Ss][Cc][Uu][Mm][Mm][Vv][Mm].[Ll][Oo][Gg] 2>/dev/null | grep -v "fell behind"
}

fail() {
  echo "DOSTEST $NAME FAILED: $1"
  program_log | tail -15
  [ -f "$RUN/OUT.TXT" ] && tr -d '\r' < "$RUN/OUT.TXT" | tail -15
  exit 1
}

# Cross-compile a test program for DOS into the run folder: build_dos_program <source> <output name> <libraries...>
build_dos_program() {
  SRC_FILE=$1; OUT_FILE=$2; shift 2
  dos_env || fail "no DJGPP cross-compiler (see dos-env.sh)"
  DJ=$DJGPP_HOME
  LIBDEPS=$DEPS
  PATH="$DJ/bin:$PATH" DJGPP="$DJ/i586-pc-msdosdjgpp/etc/djgpp.env" \
    "$DJ/bin/i586-pc-msdosdjgpp-g++" -O2 -march=i486 -x c++ -I"$LIBDEPS/include" "$SRC_FILE" \
    -o "$RUN/$OUT_FILE" -L"$LIBDEPS/lib" "$@" > "$WORK/compile-$NAME.log" 2>&1 \
    || { tail -20 "$WORK/compile-$NAME.log"; fail "cannot compile $(basename "$SRC_FILE") for DOS"; }
}

# The program was run through DOSRUN.EXE (build_dos_program ... dosrun.c), which says how it ended: it must have
# ended by itself with status 0, with no crash message (a plug-in unloaded while something still points into it
# crashes at exit, not before)
exited_cleanly() {
  [ -f "$RUN/OUT.TXT" ] || fail "no output from DOSRUN.EXE: the program did not end"
  STATUS=$(tr -d '\r' < "$RUN/OUT.TXT" | grep "DOSRUN: exit status" | tail -1)
  case "$STATUS" in
    *"exit status 0") ;;
    *) fail "the program did not end cleanly (${STATUS:-no exit status}) $(tr -d '\r' < "$RUN/OUT.TXT" | grep -m1 'Exiting due to signal')" ;;
  esac
}

ok() {
  echo "DOSTEST $NAME OK $1"
  [ -n "$KEEP" ] || true
  exit 0
}

prepare
CONF=$WORK/$NAME.conf

case "$NAME" in
  kbd)
    write_conf "$CONF" 64
    # keyb gr: the German layout, as a user's AUTOEXEC would load it
    run_dosbox "$CONF" 60 -c "mount c $RUN" -c "c:" -c "keyb gr" \
      -c "set SCUMMVM_DOS_TESTEXIT=1" -c "set SCUMMVM_DOS_KBDTEST=1" \
      -c "SCUMMVM.EXE" -c "exit"
    LOG=$(program_log)
    echo "$LOG" | grep -q "KBDTEST: FAILED" && fail "a keyboard check failed"
    echo "$LOG" | grep -q "KBDTEST: OK" || fail "the keyboard self-test did not finish"
    ok "$(echo "$LOG" | grep 'KBDTEST: OK' | sed 's/.*KBDTEST: OK //')"
    ;;

  cd)
    # A music CD image (three tracks, 300 + 450 + 225 frames) mounted as drive D: through MSCDEX
    write_conf "$CONF" 64
    python3 "$TESTS_DIR/gen-test-cd.py" "$RUN" image > /dev/null || fail "cannot make the test CD image"
    run_dosbox "$CONF" 90 -c "mount c $RUN" -c "imgmount d $RUN/AUDIO.CUE -t iso" -c "c:" \
      -c "set SCUMMVM_DOS_TESTEXIT=1" -c "set SCUMMVM_DOS_CDTEST=drive" \
      -c "SCUMMVM.EXE" -c "exit"
    LOG=$(program_log)
    echo "$LOG" | grep -q "CDTEST: disc tracks 1-3 lengths 300,450,225" || fail "the table of contents of the disc was not read right"
    echo "$LOG" | grep -q "CDTEST: FAILED" && fail "a CD audio check failed"
    echo "$LOG" | grep -q "CDTEST: OK" || fail "the CD audio self-test did not finish"
    # The engines' rip-the-tracks warning saw the disc and only logged; the SB16's silent CD input was raised
    echo "$LOG" | grep -q "No extracted CD audio tracks found: playing them from the CD in the drive" \
      || fail "the missing-tracks warning did not see the CD in the drive"
    echo "$LOG" | grep -q "the sound card's CD input was silent: SB16 mixer" || fail "the silent CD input was not raised"
    ok "$(echo "$LOG" | grep 'CDTEST: OK' | sed 's/.*CDTEST: OK //; s/!$//')"
    ;;

  nocd)
    # No CD drive at all: the program says so in the log and plays the track from a file instead
    write_conf "$CONF" 64
    python3 "$TESTS_DIR/gen-test-cd.py" "$RUN" files > /dev/null || fail "cannot make the track file"
    run_dosbox "$CONF" 90 -c "mount c $RUN" -c "c:" \
      -c "set SCUMMVM_DOS_TESTEXIT=1" -c "set SCUMMVM_DOS_CDTEST=nodrive" \
      -c "SCUMMVM.EXE" -c "exit"
    LOG=$(program_log)
    echo "$LOG" | grep -q "DOS: no CD-ROM drive found" || fail "the program did not say there is no CD drive"
    echo "$LOG" | grep -q "CDTEST: FAILED" && fail "a check failed"
    echo "$LOG" | grep -q "CDTEST: OK" || fail "the self-test did not finish"
    ok "$(echo "$LOG" | grep 'CDTEST: OK' | sed 's/.*CDTEST: OK //; s/!$//')"
    ;;

  net)
    # An emulated NE2000 card (DOSBox-X's slirp user-mode network) with its packet driver loaded and
    # Watt-32 given the address slirp hands out. The launcher is clicked through to Options, the LAN
    # tab and "Run server" (SCUMMVM_DOS_CLICKS), and the web server's index page is fetched from the
    # host through the forwarded port 12345.
    printf 'my_ip = 10.0.2.15\nnetmask = 255.255.255.0\ngateway = 10.0.2.2\nnameserver = 10.0.2.3\n' > "$RUN/WATTCP.CFG"
    printf '[cpu]\ncore=normal\ncputype=pentium_mmx\ncycles=fixed 100000\n[dosbox]\nmemsize=64\n[ne2000]\nne2000=true\nnicbase=300\nnicirq=10\nbackend=slirp\n[ethernet, slirp]\nrestricted=false\ntcp_port_forwards=12345\n' > "$CONF"
    SDL_AUDIODRIVER=dummy timeout 90 "$DOSBOX" -fastlaunch -conf "$CONF" -c "set PATH=$DOS_PATH" \
      -c "mount c $RUN" -c "c:" -c 'Z:\SYSTEM\NE2000.COM 0x60 10 0x300' -c 'set WATTCP.CFG=C:\' \
      -c "set SCUMMVM_DOS_CLICKS=${NET_CLICKS:-12:937,302;20:619,153;28:239,196}" \
      -c "SCUMMVM.EXE" -c "exit" > "$WORK/dosbox-$NAME.log" 2>&1 &
    DOSBOX_PID=$!
    PAGE=
    for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18; do
      sleep 5
      PAGE=$(curl -s --max-time 4 http://127.0.0.1:12345/ 2>/dev/null)
      case "$PAGE" in *"local webserver index page"*) break ;; esac
      PAGE=
    done
    kill "$DOSBOX_PID" 2>/dev/null
    sleep 2
    [ -n "$PAGE" ] || fail "the web server did not answer on port 12345"
    program_log | grep -q "no TCP/IP stack" && fail "the program found no TCP/IP stack"
    ok "the web server's index page came through the emulated network card"
    ;;

  plugins)
    # The plug-in build starts: the PLUGINS folder is found, with the detection plug-in and an engine plug-in
    # for each engine of the build, and the detection plug-in is loaded for the launcher
    write_conf "$CONF" 64
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE
    run_dosbox "$CONF" 90 -c "mount c $RUN" -c "c:" \
      -c "set SCUMMVM_DOS_TESTEXIT=1" -c "set SCUMMVM_DOS_PLUGLOG=1" \
      -c "DOSRUN.EXE SCUMMVM.EXE > OUT.TXT" -c "exit"
    exited_cleanly
    LOG=$(program_log)
    for f in DETECTIO.DXE SCUMM.DXE AGI.DXE SCI.DXE MOHAWK.DXE GROOVIE.DXE; do
      echo "$LOG" | grep -qi "DOSPLUGIN: found $f" || fail "$f was not found in the PLUGINS folder"
    done
    echo "$LOG" | grep -qi "DOSPLUGIN: loaded DETECTIO.DXE" || fail "the detection plug-in was not loaded for the launcher"
    echo "$LOG" | grep -q "Failed loading" && fail "a plug-in failed to load"
    ok "6 plug-ins found, the detection plug-in loaded: $(echo "$LOG" | grep -i 'DOSPLUGIN: loaded DETECTIO' | sed 's/.*loaded //; s/!$//')"
    ;;

  plugload)
    # Every engine plug-in loaded and unloaded three times, and the detection plug-in; the heap must come back
    write_conf "$CONF" 64
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE
    run_dosbox "$CONF" 150 -c "mount c $RUN" -c "c:" \
      -c "set SCUMMVM_DOS_TESTEXIT=1" -c "set SCUMMVM_DOS_PLUGLOG=1" \
      -c "set SCUMMVM_DOS_PLUGINTEST=scumm,agi,sci,mohawk,groovie" \
      -c "DOSRUN.EXE SCUMMVM.EXE > OUT.TXT" -c "exit"
    exited_cleanly
    LOG=$(program_log)
    echo "$LOG" | grep -q "PLUGINTEST: FAILED" && fail "a plug-in check failed"
    echo "$LOG" | grep -q "PLUGINTEST: OK" || fail "the plug-in self-test did not finish"
    ok "$(echo "$LOG" | grep 'PLUGINTEST: OK' | sed 's/.*PLUGINTEST: OK //; s/!$//')"
    ;;

  plugall)
    # The stable plug-in build (PLUGIN_BUILD): every one of its engine plug-ins loads and unloads, each
    # found by its file name; the heap must come back to where it was
    write_conf "$CONF" 128
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE
    run_dosbox "$CONF" 600 -c "mount c $RUN" -c "c:" \
      -c "set SCUMMVM_DOS_TESTEXIT=1" -c "set SCUMMVM_DOS_PLUGINTEST=all" \
      -c "DOSRUN.EXE SCUMMVM.EXE > OUT.TXT" -c "exit"
    exited_cleanly
    LOG=$(program_log)
    echo "$LOG" | grep -q "PLUGINTEST: FAILED" && fail "a plug-in check failed: $(echo "$LOG" | grep 'PLUGINTEST: FAILED' | head -3 | sed 's/.*PLUGINTEST: //' | tr '\n' ';')"
    echo "$LOG" | grep -q "PLUGINTEST: OK" || fail "the plug-in self-test did not finish"
    ok "$(echo "$LOG" | grep 'PLUGINTEST: OK' | sed 's/.*PLUGINTEST: OK //; s/!$//')"
    ;;

  plugadd)
    # Adding a game goes through the detection plug-in: --add finds a folder that holds nothing but the empty file
    # Testbed is detected by, and puts it in the configuration
    write_conf "$CONF" 64
    mkdir -p "$RUN/GAME"
    : > "$RUN/GAME/TESTBED"
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE
    run_dosbox "$CONF" 120 -c "mount c $RUN" -c "mount d $RUN/GAME" -c "c:" \
      -c "set SCUMMVM_DOS_PLUGLOG=1" -c 'DOSRUN.EXE SCUMMVM.EXE --add --path=D:\ --recursive > OUT.TXT' -c "exit"
    exited_cleanly
    LOG=$(program_log)
    INI=$(tr -d '\r' < "$RUN/SCUMMVM.INI" 2>/dev/null)
    echo "$LOG" | grep -qi "DOSPLUGIN: loaded DETECTIO.DXE" || fail "the detection plug-in was not loaded"
    echo "$INI" | grep -qi "^gameid=testbed" || fail "the folder was not added to the configuration"
    echo "$LOG" | grep -qi "DOSPLUGIN: unloaded DETECTIO.DXE" || fail "the detection plug-in was not unloaded afterwards"
    ok "Testbed added through the detection plug-in"
    ;;

  plugsmoke)
    # The engine's plug-in loads, the engine runs and draws, and the program leaves from inside it
    write_conf "$CONF" 64
    mkdir -p "$RUN/GAME"
    : > "$RUN/GAME/TESTBED"
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE
    run_dosbox "$CONF" 150 -c "mount c $RUN" -c "mount d $RUN/GAME" -c "c:" \
      -c "set SCUMMVM_DOS_SHOTS=5" -c "set SCUMMVM_DOS_PLUGLOG=1" -c "set SCUMMVM_DOS_QUITAT=40" \
      -c 'DOSRUN.EXE SCUMMVM.EXE -p D:\ testbed > OUT.TXT' -c "exit"
    exited_cleanly
    LOG=$(program_log)
    echo "$LOG" | grep -qi "DOSPLUGIN: loaded TESTBED.DXE" || fail "the Testbed plug-in was not loaded"
    echo "$LOG" | grep -qi "DOSPLUGIN: unloaded TESTBED.DXE" || fail "the Testbed plug-in was not unloaded"
    SHOTS=$(ls "$RUN" | grep -ci '^shot.*bmp$')
    [ "$SHOTS" -ge 3 ] || fail "only $SHOTS screenshots: the engine did not run"
    echo "$LOG" | grep -qi "ERROR:" && fail "the log has an error"
    echo "$LOG" | grep -q "no mouse driver" && fail "a no-mouse-driver warning, although DOSBox-X has a mouse driver"
    # Testbed's first frames are mostly black: some screenshot must show something
    NONBLACK=$(python3 -c "
import sys
print(max(sum(1 for b in open(f,'rb').read()[1078:][:200000] if b) for f in sys.argv[1:]))" "$RUN"/SHOT*.BMP)
    [ "$NONBLACK" -gt 1000 ] || fail "every screenshot is black"
    ok "Testbed ran through its plug-in, $SHOTS screenshots"
    ;;

  mpt)
    # libopenmpt on four module formats, through ScummVM's tracker stream (audio/mods/universaltracker.cpp): each
    # file must open as a module and give sound. The self-test runs as a Testbed game starts (OPENMPT.DXE is loaded)
    write_conf "$CONF" 64
    mkdir -p "$RUN/GAME"
    : > "$RUN/GAME/TESTBED"
    FILES=
    for ext in MOD S3M XM IT; do
      python3 "$TESTS_DIR/gen-test-mod.py" "$RUN/TEST.$ext" || fail "cannot write the test module TEST.$ext"
      FILES="$FILES${FILES:+;}TEST.$ext"
    done
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE
    run_dosbox "$CONF" 150 -c "mount c $RUN" -c "mount d $RUN/GAME" -c "c:" \
      -c "set SCUMMVM_DOS_MPTTEST=$FILES" -c "set SCUMMVM_DOS_QUITAT=30" \
      -c 'DOSRUN.EXE SCUMMVM.EXE -p D:\ testbed > OUT.TXT' -c "exit"
    exited_cleanly
    LOG=$(program_log)
    echo "$LOG" | grep "DOSOPENMPT: self-test FAILED" && fail "a test module did not play"
    RESULT=
    for ext in MOD S3M XM IT; do
      PLAYED=$(echo "$LOG" | sed -n "s/.*DOSOPENMPT: self-test: TEST.$ext played, \([0-9]*\) frames at [0-9]* Hz, peak \([0-9]*\),.*/\1 \2/p" | head -1)
      [ -n "$PLAYED" ] || fail "no self-test result for TEST.$ext"
      set -- $PLAYED
      [ "$1" -gt 0 ] && [ "$2" -gt 0 ] || fail "TEST.$ext gave no sound ($1 frames, peak $2)"
      RESULT="$RESULT $ext ($1 frames, peak $2)"
    done
    echo "$LOG" | grep -qi "ERROR:" && fail "the log has an error"
    ok "played:$RESULT"
    ;;

  mptdxe|mptdxesdl)
    # libopenmpt is OPENMPT.DXE next to the program: the launcher alone does not load it; a game of an engine with
    # tracker music (Testbed) does, a MOD file plays through it, and it goes when the engine's plug-in does
    [ -f "$RUN/OPENMPT.DXE" ] || fail "no OPENMPT.DXE next to SCUMMVM.EXE in dist/"
    ls "$RUN/PLUGINS" | grep -qi '^openmpt\.dxe$' && fail "OPENMPT.DXE is in PLUGINS, where it would be taken for an engine"
    write_conf "$CONF" 64
    mkdir -p "$RUN/GAME"
    : > "$RUN/GAME/TESTBED"
    python3 "$TESTS_DIR/gen-test-mod.py" "$RUN/TEST.MOD" || fail "cannot write the test module"
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE

    run_dosbox "$CONF" 90 -c "mount c $RUN" -c "c:" \
      -c "set SCUMMVM_DOS_PLUGLOG=1" -c "set SCUMMVM_DOS_QUITAT=12" \
      -c 'DOSRUN.EXE SCUMMVM.EXE > OUT.TXT' -c "exit"
    exited_cleanly
    LOG=$(program_log)
    echo "$LOG" | grep -q "DOSPLUGIN: loaded" || fail "the launcher run did not log its plug-ins"
    echo "$LOG" | grep -q "DOSOPENMPT: loaded" && fail "the launcher alone loaded OPENMPT.DXE"
    rm -f "$RUN"/[Ss][Cc][Uu][Mm][Mm][Vv][Mm].[Ll][Oo][Gg] "$RUN/OUT.TXT"

    run_dosbox "$CONF" 150 -c "mount c $RUN" -c "mount d $RUN/GAME" -c "c:" \
      -c "set SCUMMVM_DOS_PLUGLOG=1" -c "set SCUMMVM_DOS_MPTTEST=TEST.MOD" -c "set SCUMMVM_DOS_QUITAT=30" \
      -c 'DOSRUN.EXE SCUMMVM.EXE -p D:\ testbed > OUT.TXT' -c "exit"
    exited_cleanly
    LOG=$(program_log)
    echo "$LOG" | grep -q "DOSOPENMPT: loaded OPENMPT.DXE" || fail "starting the Testbed game did not load OPENMPT.DXE"
    echo "$LOG" | grep -q "DOSOPENMPT: self-test FAILED" && fail "the test module did not play"
    PLAYED=$(echo "$LOG" | sed -n 's/.*DOSOPENMPT: self-test: TEST.MOD played, \([0-9]*\) frames at [0-9]* Hz, peak \([0-9]*\), 1 module(s) open.*/\1 \2/p' | head -1)
    [ -n "$PLAYED" ] || fail "no self-test result for TEST.MOD"
    set -- $PLAYED
    [ "$1" -gt 0 ] && [ "$2" -gt 0 ] || fail "the test module gave no sound ($1 frames, peak $2)"
    # The module goes after the engine's plug-in, not before
    ORDER=$(echo "$LOG" | grep -io "DOSPLUGIN: unloaded TESTBED.DXE\|DOSOPENMPT: unloaded OPENMPT.DXE" | tr 'a-z\n' 'A-Z ')
    case "$ORDER" in
      *"DOSPLUGIN: UNLOADED TESTBED.DXE DOSOPENMPT: UNLOADED OPENMPT.DXE"*) ;;
      *) fail "OPENMPT.DXE was not unloaded after the Testbed plug-in ($ORDER)" ;;
    esac
    tr -d '\r' < "$RUN/OUT.TXT" | grep -qi "found openmpt.dxe" && fail "OPENMPT.DXE was taken for a plug-in"
    echo "$LOG" | grep -qi "ERROR:" && fail "the log has an error"
    ok "OPENMPT.DXE loaded for the game only, TEST.MOD played ($1 frames, peak $2), unloaded with the engine"
    ;;

  pageflip|pageflipbanked|pageflip16)
    # The self-check (SCUMMVM_DOS_VERIFY) moves the cursor every frame and compares video memory with the picture
    # it should show, which is what catches a stale part of the hidden page
    write_conf "$CONF" 64
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE
    BANK=
    [ "$NAME" = pageflipbanked ] && BANK=1
    GUIMODE=640x480
    GUIDEPTH=8
    if [ "$NAME" = pageflip16 ]; then
      GUIMODE=800x600
      GUIDEPTH=16
    fi
    run_dosbox "$CONF" 120 -c "mount c $RUN" -c "c:" \
      -c "set SCUMMVM_DOS_GUI_MODE=$GUIMODE" -c "set SCUMMVM_DOS_GUI_DEPTH=$GUIDEPTH" -c "set SCUMMVM_DOS_VERIFY=1" \
      -c "set SCUMMVM_DOS_QUITAT=25" ${BANK:+-c "set SCUMMVM_DOS_BANKED=1"} \
      -c 'DOSRUN.EXE SCUMMVM.EXE > OUT.TXT' -c "exit"
    exited_cleanly
    LOG=$(program_log)
    echo "$LOG" | grep -q "2 display pages" || fail "the display did not use two pages"
    echo "$LOG" | grep -q "DOSGFX verify: [0-9]* frames checked" || fail "the self-check did not run"
    echo "$LOG" | grep -q "DOSGFX verify: frame" && fail "a frame on the display differs from the picture"
    echo "$LOG" | grep -qi "did not switch display pages" && fail "the card refused to switch pages"
    ok "$(echo "$LOG" | grep 'DOSGFX verify: [0-9]* frames checked' | tail -1 | sed 's/.*verify: //; s/!$//')"
    ;;

  smoke)
    # The launcher comes up and draws
    write_conf "$CONF" 64
    run_dosbox "$CONF" 60 -c "mount c $RUN" -c "c:" \
      -c "set SCUMMVM_DOS_SHOTS=5" -c "SCUMMVM.EXE" -c "exit"
    SHOTS=$(ls "$RUN" | grep -ci '^shot.*bmp$')
    [ "$SHOTS" -ge 3 ] || fail "the launcher: only $SHOTS screenshots, the program did not run"
    program_log | grep -qi "ERROR:" && fail "the log has an error"
    # A black screen would mean it drew nothing
    LAST=$(ls "$RUN"/SHOT*.BMP | tail -2 | head -1)
    NONBLACK=$(python3 -c "
import sys
d=open(sys.argv[1],'rb').read()[1078:]
print(sum(1 for b in d[:200000] if b))" "$LAST")
    [ "$NONBLACK" -gt 1000 ] || fail "the launcher: the screen is black"
    ok "the launcher ran, $SHOTS screenshots"
    ;;

  nodpmi)
    # The negative control for the DPMI host: dist without CWSDPMI.EXE, on the tests' PATH. DJGPP's start-up code
    # says it found no DPMI host and stops before the program runs, so no log is written.
    write_conf "$CONF" 64
    rm -f "$RUN/CWSDPMI.EXE"
    run_dosbox "$CONF" 60 -c "mount c $RUN" -c "c:" -c "SCUMMVM.EXE > OUT.TXT" -c "exit"
    [ -f "$RUN/OUT.TXT" ] || fail "no output: the program was not started"
    tr -d '\r' < "$RUN/OUT.TXT" | grep -q "no DPMI" || fail "the program started without dist's CWSDPMI.EXE: another DPMI host is on the test machine"
    program_log | grep -q . && fail "the program ran and wrote its log without dist's CWSDPMI.EXE"
    ok "without dist's CWSDPMI.EXE: $(tr -d '\r' < "$RUN/OUT.TXT" | grep 'no DPMI' | head -1)"
    ;;

  sdl)
    # The SDL3 build: SDL's DOS video and audio drivers start, the launcher draws, the program is left cleanly
    write_conf "$CONF" 64
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE
    run_dosbox "$CONF" 90 -c "mount c $RUN" -c "c:" \
      -c "set SCUMMVM_DOS_SHOTS=2" -c "set SCUMMVM_DOS_QUITAT=12" \
      -c 'DOSRUN.EXE SCUMMVM.EXE -d1 > OUT.TXT' -c "exit"
    exited_cleanly
    LOG=$(program_log)
    echo "$LOG" | grep -q 'Using SDL Video Driver "vesa"' || fail "SDL's VESA video driver was not used"
    echo "$LOG" | grep -q 'Using SDL Audio Driver "soundblaster"' || fail "SDL's Sound Blaster audio driver was not used"
    echo "$LOG" | grep -qi "ERROR:" && fail "the log has an error"
    echo "$LOG" | grep -q "no mouse driver" && fail "a no-mouse-driver warning, although DOSBox-X has a mouse driver"
    SHOTS=$(ls "$RUN" | grep -ci '^shot.*png$')
    [ "$SHOTS" -ge 3 ] || fail "only $SHOTS screenshots: the launcher did not run"
    # A blank screen compresses to almost nothing; the launcher's picture does not
    BIG=$(find "$RUN" -iname 'SHOT*.PNG' -size +8k | wc -l | tr -d ' ')
    [ "$BIG" -ge 1 ] || fail "every screenshot is blank"
    ok "the launcher ran on SDL3, $SHOTS screenshots"
    ;;

  scalesdl)
    # The SDL3 build: scale_factor=3 for every game, and SCUMMVM_DOS_MODE_MAX hides the video modes above 800x600, as
    # a card with less video memory would. SDL's DOS driver has no renderer to shrink a picture, so a screen larger
    # than the video mode would be cut off: the 800x600 launcher stays at 1x (not 2400x1800) and the 320x200 Testbed
    # game is lowered from 3x (960x600) to 2x (640x400)
    write_conf "$CONF" 64
    mkdir -p "$RUN/GAME"
    : > "$RUN/GAME/TESTBED"
    printf '[scummvm]\r\nscaler=normal\r\nscale_factor=3\r\n\r\n[testbed]\r\ngameid=testbed\r\nengineid=testbed\r\npath=D:\\\r\n' \
      > "$RUN/SCUMMVM.INI"
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE
    run_dosbox "$CONF" 150 -c "mount c $RUN" -c "mount d $RUN/GAME" -c "c:" \
      -c "set SCUMMVM_DOS_MODE_MAX=800x600" -c "set SCUMMVM_DOS_RTL=15" -c "set SCUMMVM_DOS_QUITAT=30" \
      -c 'DOSRUN.EXE SCUMMVM.EXE -d1 testbed > OUT.TXT' -c "exit"
    exited_cleanly
    LOG=$(program_log)
    echo "$LOG" | grep -q "DOS test: returning to the launcher" || fail "the program was not sent back to the launcher"
    SCREENS=$(echo "$LOG" | sed -n 's/.*DOS: screen \([0-9]*x[0-9]*\) in a .*/\1/p' | tr '\n' ' ')
    set -- $SCREENS
    [ "$1" = 800x600 ] || fail "the launcher started at ${1:-no size}, not 800x600 at 1x (screens: $SCREENS)"
    echo "$SCREENS" | grep -q "640x400" || fail "the game was not lowered to 640x400 (2x) (screens: $SCREENS)"
    echo "$SCREENS" | grep -q "960x600" && fail "a 960x600 screen was set up, larger than every mode (screens: $SCREENS)"
    LAST=$(echo "$SCREENS" | awk '{ print $NF }')
    [ "$LAST" = 800x600 ] || fail "back in the launcher the screen is $LAST, not 800x600 (screens: $SCREENS)"
    echo "$LOG" | grep -q "320x200 at 3x needs a larger video mode than the card has; using 2x" ||
      fail "the log does not say the game's scale factor was lowered"
    echo "$LOG" | grep -q "DOS: only .* screen can be shown" && fail "a screen larger than its video mode was set up"
    echo "$LOG" | grep -qi "ERROR:" && fail "the log has an error"
    ok "3x on an 800x600 card: launcher at 1x, game lowered to 2x (screens: $SCREENS)"
    ;;

  nomouse|nomousesdl)
    # DOSBox-X's own mouse driver switched off (nomouse: the own backend's Testbed build, nomousesdl: SDL3's): the
    # program says so once in the log and again on leaving (in text mode, where it can be read), and still runs and
    # ends cleanly
    write_conf "$CONF" 64
    printf '[dos]\nint33=false\n' >> "$CONF"
    build_dos_program "$TESTS_DIR/dos-programs/dosrun.c" DOSRUN.EXE
    run_dosbox "$CONF" 90 -c "mount c $RUN" -c "c:" \
      -c "set SCUMMVM_DOS_QUITAT=8" \
      -c 'DOSRUN.EXE SCUMMVM.EXE > OUT.TXT' -c "exit"
    exited_cleanly
    WARNINGS=$(program_log | grep -c "WARNING: DOS: no mouse driver is loaded")
    [ "$WARNINGS" = 1 ] || fail "$WARNINGS no-mouse-driver warnings in the log, not 1"
    tr -d '\r' < "$RUN/OUT.TXT" | grep -q "^ScummVM: no mouse driver is loaded" || fail "no reminder on leaving"
    ok "no mouse driver: one warning in the log, a reminder on leaving, a clean exit"
    ;;

  *)
    echo "unknown DOS test '$NAME'"
    exit 2
    ;;
esac
