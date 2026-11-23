# Tests of the DOS port

Two kinds: unit tests that run on the build machine (the way ScummVM tests everything else), and a few runtime tests that run the DOS program in DOSBox-X for what needs a DOS machine. None of the tests needs a game.
Every test prints one decisive line (`SUITE OK ...`, `DOSTEST <name> OK ...`) and exits non-zero otherwise.

## Unit tests (CxxTest, part of `make test`)

The DOS port keeps its arithmetic and byte layouts in headers with no DOS calls in them, so they build on any machine.
The suites are in `test/backends/dos/` (`test/module.mk` takes every `*.h` there):

| Suite | Tests | Of |
|---|---|---|
| `DosKeymapTestSuite` | keyboard layouts: BIOS character + scan code to a key, in the DOS code page | `dos-keymap.h`, `dos-bioskey.h`, `dos-codepages.h` |
| `DosCdTestSuite` | CD audio: Red Book addresses, tracks, MSCDEX requests and answers, volume, repeats | `dos-cd.h` |
| `DosDitherTestSuite` | 256-colour dithering: palette, tables, the line converter against the plain one | `dos-dither.h` |
| `DosModesTestSuite` | choosing the launcher's resolution (EDID, VESA mode lists) | `dos-modes.h` |
| `DosNamesTestSuite` | 8.3 names, CONFIG.SYS country to language | `dos-names.h` |
| `DosNetParseTestSuite` | dotted addresses and port byte order | `dos-netparse.h` |
| `DosJoystickTestSuite` | game port buttons, calibration, cursor speed | `dos-joystick.h` |
| `DosFileSysTestSuite` | engine data found by its long name when DOS lists it under the 8.3 one; savegames with long names found, written and renamed with and without a long file name driver, in both directions (a folder made for the test, `test/system/temp_directory.h`) | `dos-filesys.h` |
| `DosBannerTestSuite` | the message shown while a game starts: its size on each resolution, broken into lines that fit the screen | `dos-banner.h` |

They need a native build tree (default `build-host-test` in the source folder, or `SCUMMVM_HOST_TREE=...`), configured from this source
folder with a backend that needs no screen. Make it from the source folder with:

    mkdir build-host-test && cd build-host-test
    ../configure --backend=null --disable-all-engines --enable-engine=twine,wintermute,ultima \
      --enable-release --disable-debug --disable-nasm --disable-fluidsynth --disable-fluidlite --disable-sonivox \
      --disable-mt32emu --disable-libcurl --disable-cloud --disable-sdlnet --disable-discord --disable-tts \
      --disable-taskbar --disable-system-dialogs --disable-sparkle --disable-openmpt --disable-mikmod

(`check-host-tree.sh` checks it.) The generator for the test runner starts `python`; macOS has `python3` only, so
`common.sh` puts a link called `python` in the tree's `bin/`.

    sh host-suite.sh DosCdTestSuite 8     # one suite: builds the runner, checks it has at least 8 tests, runs them
    sh host-make-test.sh                  # `make test`: all of ScummVM's tests and these; MIN_TESTS (default 561) must run
    sh check-dos-suites.sh                # every DOS suite above exists, is part of the build, has enough tests and passes

## Runtime tests (DOSBox-X)

    sh run-dos-test.sh <name>             # one
    sh run-dos-tests.sh                   # all, one after the other: prints ALL DOS TESTS OK

`DOS_BUILD` is the build folder whose `dist/` is run (default `build-dos-stable` in the source folder; `make-dist.sh` makes `dist/`).
The builds these tests use (`build-dos-stable` with the engines as plug-ins, as every build is made by default, and `build-dos-stable-static` with them inside the program, the control the plug-in checks measure against)
are made from their recipes in `common.sh` (`configure_named_build`) by `sh rebuild-builds.sh stable stable-static testbed sdl-testbed`, so a fresh clone needs
only the DJGPP cross-compiler and the cross-built libraries, found through `DJGPP_HOME` and `DEPS` (see `../dos-env.sh`).
All the folders the tests make are `build-...` folders in the source folder, which `/build*` in `.gitignore` keeps out of git status.
Each test works in `build-dos-tests/run-<name>`, in a clean copy of the distribution, and reads the program's own log
(`SCUMMVM.LOG`) for the lines the self-test hooks write.

| Name | What it proves | Hook |
|---|---|---|
| `nodpmi` | without dist's `CWSDPMI.EXE` the program stops at start-up with DJGPP's "no DPMI" message: the test machine has no other DPMI host (every test leaves DOSBox-X's own `Z:\BIN\CWSDPMI.EXE` off the `PATH`), so the other tests ran on the one `make-dist.sh` shipped | none |
| `kbd` | with `KEYB GR` loaded, keys give the German layout's characters | `SCUMMVM_DOS_KBDTEST` |
| `cd` | MSCDEX: a CD image of three audio tracks is found, read, played, timed, stopped | `SCUMMVM_DOS_CDTEST=drive` |
| `nocd` | no CD drive: one WARNING, and the track file plays instead | `SCUMMVM_DOS_CDTEST=nodrive` |
| `net` | a browser on the host gets the launcher's web server through an emulated NE2000 card | `SCUMMVM_DOS_CLICKS` |
| `pageflip`, `pageflipbanked`, `pageflip16` | page flipping: the launcher draws to a hidden display page; on every frame, with the cursor moving, video memory matches the picture (640x480 in 256 colours, linear or banked; 800x600 in high colour) | `SCUMMVM_DOS_VERIFY` |
| `plugins` | the plug-in build (`build-dos-stable`, or `PLUGIN_BUILD`) finds its PLUGINS folder, the detection plug-in and an engine plug-in for each engine, and loads the detection plug-in for the launcher | `SCUMMVM_DOS_PLUGLOG` |
| `plugload` | every engine plug-in, and the detection plug-in, loaded and unloaded three times: found by engine id, one in memory at a time, the heap back to where it was; the program then ends with status 0 | `SCUMMVM_DOS_PLUGINTEST` |
| `plugall` | the stable plug-in build (`PLUGIN_BUILD`): every one of its engine plug-ins (about 110) loads and unloads once, found by its file name, and the heap comes back to where it was | `SCUMMVM_DOS_PLUGINTEST=all` |
| `plugadd` | `--add --path=... --recursive` with no game data: a folder holding only an empty file `TESTBED` is found through the detection plug-in, added as `testbed`, and the plug-in is unloaded again (the `testbed` build, `TESTBED_BUILD`) | `SCUMMVM_DOS_PLUGLOG` |
| `plugsmoke` | with no game data: the Testbed engine runs through its plug-in, draws, and is left from inside it with the plug-in unloaded | `SCUMMVM_DOS_QUITAT` |
| `smoke` | the launcher comes up and draws | `SCUMMVM_DOS_SHOTS` |
| `sdl` | the SDL3 build (`--backend=sdl`, `build-dos-sdl-testbed`, or `SDL_BUILD`): SDL's VESA video and Sound Blaster audio drivers start, the launcher draws, the program is left cleanly | `SCUMMVM_DOS_SHOTS`, `SCUMMVM_DOS_QUITAT` |

`SCUMMVM_DOS_TESTEXIT=1` makes the program leave as soon as the self-tests have run.

Test data is made by `gen-test-cd.py` (a cue/bin CD image of three
audio tracks of 300, 450 and 225 frames, and a WAV of track 2); nothing is downloaded.

## Support

* `check-openmpt-lib.sh`: libopenmpt is cross-built where `configure` looks for it
* `rebuild-builds.sh`, `check-builds.sh`: rebuild the DOS builds; check they are current
* `run-dxe-experiment.sh` (with `dos-programs/dxetest/`): a C++ module linked by the plug-in build's own script (`dxe-link.sh`) and loaded with DJGPP's `dlopen` does what an engine plug-in does: virtual calls, global constructors and destructors, `std::string`, exceptions in both directions, `dlclose`. `measure-dxe-engines.sh`: packages the engines of a finished static build as DXE modules and reports their sizes and what they need from the program (the measurement that came before the plug-in build)
* `check-plugin-config.sh`, `check-plugin-build.sh`, `check-plugin-ram.sh`: the plug-in build is configured (and a static one left alone), built (plug-ins with 8.3 names, the program free of engines, the export table complete), and needs less RAM to reach the launcher: see `../PLUGINS.md`. `check-no-regression.sh`: `make test` and the DOS tests of the static build still pass
* `check-docs.sh`: the notes (`FEATURES.md`, this file, the libraries' README) describe what the port does now
* `tools/gen-codepages.py` (one folder up) regenerates `dos-codepages.h` from Python's codecs
