# DOS plug-ins: engines as DXE modules, loaded one at a time

DOS loads a whole program into memory before it starts, so a build with every engine in it needs about 100 MB of RAM
to reach the launcher, however few engines the player's games use. With plug-ins the engines (and the detection
tables) are separate files, `.DXE` modules in a `PLUGINS` folder; the program loads only the one a game needs.

**This is how every DOS build is made by default.** `DOS_STATIC=1` in front of `configure-dos.sh` links the engines
into the program instead (the all-in-one build, kept for comparison and for the tests that need it).

## What it saves

Measured under DOSBox-X with 128 MB (so that nothing is swapped out), as the memory the program holds when its
launcher is on screen (`tests/check-plugin-ram.sh`):

| Build | Engines inside the program | Engines as plug-ins |
|---|---|---|
| Every engine (112 plug-ins) | 110 MB | **32 MB** |

The loaded program alone (from its section sizes) goes from 93.9 to 15.6 MB for every engine. At the launcher the
detection tables are loaded too (6.5 MB for every engine);
ScummVM lets them go before a game starts, and an engine takes their place: from 0.1 MB (Groovie) to about 5 MB for the
largest, 0.7 MB on average. The same measurement while a game runs (Sam & Max, SCUMM, 40 seconds in; measured by hand, no
test needs a game):

| Build | Engines inside the program | Engines as plug-ins |
|---|---|---|
| Every engine | 97 MB | **22 MB** |

The RAM no longer depends on how many engines are installed. The disk space does: the all-engines build is 19 MB of
program and 84 MB of plug-ins.

## Making and running it

    backends/platform/dos/configure-dos.sh stable
    make -C build-dos-stable-sdl -j8
    make -C build-dos-stable-sdl dosdist

`make dosdist` runs `make-dist.sh` (which can also be run on its own: `make-dist.sh <build> <build>/dist`) and zips
`dist/` into the build folder as the release archive, `scummvm-<version>-dos.zip` (`-dos-static.zip` for a
`DOS_STATIC=1` build; `DOSDIST_ZIP=name.zip` names it otherwise). Everything in it is inside a `SCUMMVM` folder, so
it unpacks on the DOS machine as `C:\SCUMMVM`. Like other ports' releases it has ScummVM's documents and the licences
of the code in ScummVM (`DIST_FILES_DOCS`), in `DOC` as DOS text files with 8.3 names (`README.TXT`, `COPYING.LGP`,
...). `make-dist.sh` takes the libraries' licences and `CWSDPMI.EXE` from `DEPS` and `DEPS_ROOT` (by default
`build-dos-deps` in the source folder, see `dos-env.sh`): set them as for `configure-dos.sh` when the libraries are
elsewhere.

`configure-dos.sh` adds `--enable-plugins --default-dynamic --enable-detection-dynamic` for every profile unless
`DOS_STATIC=1` is set. (`DOS_PLUGINS=1`, which used to switch it on, is now the same as not setting anything.) The
test builds that `tests/rebuild-builds.sh` makes each have a recipe in `tests/common.sh` that says which kind it is
(`stable` and `stable-sdl` have plug-ins, as every build does by default; `stable-static` has the engines inside the
program and is only the control the plug-in checks measure against).
`make-dist.sh` puts the program and its `PLUGINS` folder side by side:

    SCUMMVM.EXE  OPENMPT.DXE  CWSDPMI.EXE  SCUMMREM.ZIP ...  DATA\  PLUGINS\AGI.DXE SCUMM.DXE ... DETECTIO.DXE

Run ScummVM from that folder, as before (the plug-ins are found relative to the current folder, like DATA). The
launcher, `--add`, and `scummvm -p path gameid` work as in a static build. Nothing is needed on the DOS side beyond what
a static build needs (the sound card's settings, ...; `make-dist.sh` puts the DPMI host, `CWSDPMI.EXE`, next to the program).

## Tracker music: OPENMPT.DXE

libopenmpt (MOD, S3M, XM, IT and the other module formats, for AGS, SLUDGE and Testbed) is a module of its own in a
plug-in build, `OPENMPT.DXE` next to `SCUMMVM.EXE`, not part of the program. It is loaded when a game of one of those
engines starts (`OSystem::engineInit()`, `dos-openmpt.cpp`) and unloaded with the engine's plug-in, once no module is
left open; the launcher and the games of every other engine never load it. Should another engine open a module, its
first tune loads it. Without the file, tracker music is off (one WARNING in the log) and the game runs.

* **The program's side** (`dos-openmpt.cpp`): the six libopenmpt functions `audio/mods/universaltracker.cpp` calls,
  each passing the call on to the module, which gives them as one table (`dosOpenMPTGetApi()`, `dos-openmpt-dxe.cpp`).
  The engines whose `configure.engine` needs `universaltracker` are listed in `dos-openmpt.h`
  (`tests/check-openmpt-dxe.sh` keeps that list right).
* **The build** (`dos.mk`): `-lopenmpt` is taken off the program's link and linked into `openmpt.dxe` by `dxe-link.sh`
  instead (with `-E _dosOpenMPT`, its one export); what the module needs from the C++ runtime, libc and zlib comes from
  the program's export table, like a plug-in's. `make-dist.sh` copies it. `DOS_STATIC=1` builds keep libopenmpt inside
  the program.

What it saves (`i586-pc-msdosdjgpp-size`, text + data + bss of the program; the RAM as in the next section):

| Build | libopenmpt in the program | In OPENMPT.DXE |
|---|---|---|
| stable (own backend) | 15.9 MB | **13.1 MB** |
| stable on SDL3 | 17.2 MB | **14.4 MB** |
| Testbed only (own backend) | 11.5 MB | **8.7 MB** |

The program is 2.8 MB smaller in every build. At the launcher the stable build (own backend, 128 MB under DOSBox-X)
holds 29.4 MB instead of 32.2 MB, the same 2.8 MB less. While a Testbed game runs, `OPENMPT.DXE` takes 2.8 MB of
heap (loaded in 0.37 s under DOSBox-X at 100 million instructions a second) and gives all of it back when unloaded
(`tests/run-dos-test.sh mptdxe`). On disk it is 2.5 MB.

## Small machines

The figures above are what the program holds, measured under DOSBox-X with 128 MB; DOS, the DPMI host and the disk cache
come on top, and none of it has been measured on a machine with 32 MB. Even so, they say which builds cannot fit: the
`stable` profile holds 29 MB at the launcher (32 MB with libopenmpt in the program; the detection tables alone are 6.5 MB, and the 112 engine
plug-ins are only loaded one at a time), which is nearly all of a 32 MB machine. For a small machine build with only the
engines whose games are installed (`ENGINES_ONLY="scumm agi sci"`, see `ENGINES.md`).

## How it works

* **ScummVM's own mechanism.** `--enable-plugins` is ScummVM's plug-in support; a platform with little memory
  (3DS, Miyoo, Dingux, ...) uses the "uncached" plug-in manager: the detection tables are one plug-in, loaded for the
  launcher and for `--add` and unloaded when a game starts, and one engine plug-in is in memory at a time. The DOS
  port gives it a loader and a build for DXE3 modules, DJGPP's `dlopen`.
* **The loader** (`backends/plugins/dos/dos-provider.cpp`, `DOSPluginProvider`): lists `PLUGINS`, matches the suffix in
  any case, `dlopen`s, finds `PLUGIN_getVersion` and the other entry points with `dlsym`, `dlclose`s.
* **The link** (`dxe-link.sh`, named by configure as the linker for `plugins/*.dxe`): `dxe3gen` with unresolved symbols
  allowed (the program supplies them) and only the `PLUGIN_` entry points exported. It links in
  `backends/plugins/dos/dos-plugin-eh.cpp`: `dlopen` runs a module's constructors but does not tell the C++ runtime where
  its exception unwind tables are, so without this an exception thrown in a plug-in aborts the program. Each module is
  linked in a folder of its own (`plugins/.dxe-work/`): `dxe3gen` writes a temporary file in the current folder, and two
  at once (make -j) mixed their objects up.
* **The export table** (`dos.mk`, `dxe-exports.sh`): `dxe3res` reads what the finished plug-ins leave unresolved and writes
  the table of those symbols (3,172 for every engine); it is compiled with the C compiler and linked
  first in the program. Linking it is also what makes the linker keep the library code the plug-ins call.
* **8.3 names.** `PLUGIN_NAME` (Makefile.common, redefined for DOS in config.mk) cuts a plug-in's file name to 8 characters
  (`bladerunner` is `BLADERUN.DXE`; no two collide), `PLUGIN_NAME_MAX` (base/plugins.cpp) does the same for the lookup
  by engine id, and the detection plug-in is `DETECTIO.DXE` (`PLUGIN_DETECTION_NAME`, as on MiNT).

## Rebuilding after a change

Each engine is its own plug-in, so `make` compiles only the files of the engine that changed (and of the engines that
include a header that changed) and links that engine's plug-in; the other plug-ins are not touched. The program is not
linked again either, unless the export table changed: `dxe-exports.sh` writes the table only if it differs from the last
one, so its object file, `base/version.o` and the program keep their time stamps. One AGI file touched, make alone, on
this machine:

| Build | Engines in the program | Engines as plug-ins |
|---|---|---|
| Every engine | 15.3 s (link of the 123 MB program) | **6.8 s** |

(With nothing changed, `make` itself takes 4 s in the all-engines build: it reads the dependency files of 110 engines.)

Before the table was written only when it changed, a plug-in build also compiled the table, rebuilt `base/version.o` and
linked the program every time: 14 s for the same change in the all-engines build. A static build has to link the whole
program again for any engine change. `tests/rebuild-builds.sh` runs configure only if something configure reads is
newer than the build's configuration (`FORCE_CONFIGURE=1` to make it), and makes `dist/` again only if the program or a
plug-in is newer than it.

## Changes outside the DOS folder

* `configure`: DOS no longer forces plug-ins off; with `--enable-plugins`, the DOS plug-in settings and `dos.mk`.
* `rules.mk`, `Makefile.common`: `PLUGIN_NAME`, the file name of a plug-in as a function of its folder (identity by default).
* `base/plugins.cpp`, `base/plugins.h`: `PLUGIN_NAME_MAX` in the lookup by engine id; and a search for a game by id used to
  load the engine plug-ins one after another and list the game once for each of them (so `scummvm -p path gameid` called
  every game ambiguous); with the detection of all engines already in memory it now looks once
  (`PluginManager::isAllDetectionLoaded()`), which also saves loading every engine for the search.
* `backends/module.mk`, `backends/platform/dos/module.mk`: the provider and the export table in the DOS build.

## Tests

`tests/run-dos-test.sh plugins | plugload | plugall | plugsmoke | plugadd`, `tests/run-dxe-experiment.sh`,
`tests/check-plugin-config.sh`, `check-plugin-build.sh`, `check-plugin-ram.sh`; see `tests/README.md`. In short: the
detection plug-in and every engine plug-in (112 of them) load and unload, found by engine id or file name, with the heap
back to where it started; the Testbed engine runs through its plug-in and the program ends with status 0 with it loaded;
`--add` goes through the detection plug-in. Test hooks: `SCUMMVM_DOS_PLUGLOG=1` (what each plug-in takes to load and
what comes back), `SCUMMVM_DOS_PLUGINTEST=<ids>|all`, `SCUMMVM_DOS_QUITAT=<seconds>`.

## What it costs, and what is not known

* **Load time.** Under DOSBox-X at 100 million instructions a second: a plug-in takes 0.13 s (AGI) to 1.0 s (SCUMM,
  Director) to load, 0.3 s on average, and the detection tables of every engine 1.1 s. It grows with the export table
  (SCUMM took 1.0 s with 3,172 symbols): `dlopen` looks each symbol up in
  the tables one by one. Unloading takes a few milliseconds. Also measured on a real Pentium.
* **Unloading** gives all of a plug-in's memory back (0 KB left over in every case tried); what an engine leaves behind in
  the program's own objects would show as a crash at the next load or at exit, and none was seen.
* **Exceptions in plug-ins** are tried with a small test module (virtual calls, constructors, `std::string`, exceptions in
  both directions), and an exception thrown by a real engine at run time was tried on real hardware.
* **Real hardware:** tried on a real DOS machine, with the plug-ins read from a real disk and from a CD, playing games of
  engines other than SCUMM through their plug-ins.
