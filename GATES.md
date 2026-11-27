# Gates: libopenmpt as OPENMPT.DXE, loaded only when a tracker-music engine's game starts

OWNS: backends/platform/dos/**, backends/plugins/dos/**, GATES.md, .unlazy/_checks/**, .unlazy/_archive/**

Scope: in plug-in builds (the default) libopenmpt is no longer linked into SCUMMVM.EXE; it is a DXE module,
OPENMPT.DXE, next to the program, loaded when a game of an engine with tracker music (AGS, SLUDGE, Testbed) starts
and unloaded with that engine's plug-in. DOS_STATIC builds keep it inside the program. Both DOS builds (own backend
and SDL3) rebuilt. Baselines measured before the change: .unlazy/_checks/openmpt-baseline.txt.

- [x] G1: the plug-in builds (stable, stable-sdl, testbed, sdl-testbed) and the static control rebuild from the current sources with no warning on a changed line, and their dist/ is refreshed
  CHECK: sh backends/platform/dos/tests/rebuild-builds.sh stable stable-sdl testbed sdl-testbed stable-static > /dev/null && sh .unlazy/_checks/build.sh build-dos-stable build-dos-stable-sdl build-dos-testbed build-dos-sdl-testbed
  EXPECT: BUILD OK build-dos-stable build-dos-stable-sdl build-dos-testbed build-dos-sdl-testbed
  EVIDENCE: automatic-evidence=v1; definition-sha256=19b9b0602b9abc87383ed0c7a95f18a4b374eac4ac6cc3687a911aed132501da; exit=0; EXPECT=matched; output-sha256=d68c197f01c774de0956e43326c0c78f8f5ed281b3c56b68073a9e962a479eff; output-bytes=87; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G2: in every plug-in build the program holds no libopenmpt code and does not link it, openmpt.dxe is built and exports only its entry point, and dist/ has OPENMPT.DXE beside SCUMMVM.EXE (not in PLUGINS); the static build still has libopenmpt inside
  CHECK: sh .unlazy/_checks/openmpt-check.sh layout
  EXPECT: OPENMPT LAYOUT OK
  EVIDENCE: automatic-evidence=v1; definition-sha256=837f3bbb267b65367af81cff55c8153969446c0fd1479d90655b919ef7d8201f; exit=0; EXPECT=matched; output-sha256=3e04404317146631400fae6006a88ec8defa3ac3205fc74706f011a1fd3c907b; output-bytes=438; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G3: each plug-in build's program (text+data+bss) is at least 1 MB smaller than before the change
  CHECK: sh .unlazy/_checks/openmpt-check.sh size
  EXPECT: OPENMPT SIZE OK
  EVIDENCE: automatic-evidence=v1; definition-sha256=236771024fc8026a0ef021815a638f0abe7eb9d3e56faa8518bbfc516e04eb95; exit=0; EXPECT=matched; output-sha256=7653dd1c2f1bf18388e0ddc18b3623bb55ecc24aaec14e3806c19dcf0942bb3b; output-bytes=229; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G4: the own-backend stable build holds at least 1 MB less RAM at its launcher than before (DOSBox-X, 128 MB)
  CHECK: sh .unlazy/_checks/openmpt-check.sh ram
  EXPECT: OPENMPT RAM OK
  EVIDENCE: automatic-evidence=v1; definition-sha256=8b7d1f7c6849683a06102373e9d3d7752284af27a13b62f1eaf2f2fc1b5276b6; exit=0; EXPECT=matched; output-sha256=33b051f6e9dfcb2bb99cc08504e347b212f2889815057a8af570feb7218b9efa; output-bytes=85; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G5: in DOSBox-X on the own backend: the launcher alone does not load OPENMPT.DXE; starting a Testbed game loads it, a MOD file plays through it with sound, it is unloaded when the engine's plug-in is, and the program ends with status 0
  CHECK: sh backends/platform/dos/tests/run-dos-test.sh mptdxe
  EXPECT: DOSTEST mptdxe OK
  EVIDENCE: automatic-evidence=v1; definition-sha256=ccfb95a1ac3c39fd40fbe35aebdfe44527fc288c435fa8247344b8fe431640a3; exit=0; EXPECT=matched; output-sha256=4f0c067a79f61dc1ba832abccc270655dd7273c215a0018c03f3a2e9b69011f1; output-bytes=125; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G6: the same on the SDL3 build
  CHECK: sh backends/platform/dos/tests/run-dos-test.sh mptdxesdl
  EXPECT: DOSTEST mptdxesdl OK
  EVIDENCE: automatic-evidence=v1; definition-sha256=ab4de0328459e77afa0092af7900f0b3faa7e34556928b16c2b8bb301a6a8139; exit=0; EXPECT=matched; output-sha256=86d2da6297c57e5bbe77020aeb2509fda00b0c71cc22b86459f815fda0e1ed8d; output-bytes=128; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G7: the engines the program loads OPENMPT.DXE for are exactly the engines whose configure.engine needs universaltracker, and a removed DXE is caught by the check (negative control)
  CHECK: sh backends/platform/dos/tests/check-openmpt-dxe.sh && sh .unlazy/_checks/openmpt-check.sh negative
  EXPECT: /OPENMPT DXE OK[\s\S]*OPENMPT NEGATIVE OK/
  EVIDENCE: automatic-evidence=v1; definition-sha256=c7625885ffd9ab539dabd5184286d42d4e4d9b8764c43adf612643782055b79e; exit=0; EXPECT=matched; output-sha256=7df763f290a2cbfdc1bad2d999bc177ab3dcb7830fa8073c4a530ba638b4ff53; output-bytes=65; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G8: every earlier DOS runtime test still passes in DOSBox-X
  CHECK: sh .unlazy/_checks/dos-runtime.sh
  EXPECT: ALL DOS TESTS OK (15 tests)
  EVIDENCE: automatic-evidence=v1; definition-sha256=6404f66e7ada85d6ea1ee393c3a1ff32b881c5181fdb685fe0bbdc7f7469b359; exit=0; EXPECT=matched; output-sha256=1956d789f3e3f3d95feaaa30665d9e0359a5dacb544872d25d5583393973bfa4; output-bytes=1012; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G9: every changed source line is formatted the way the project's astyle options format it
  CHECK: node .unlazy/_checks/astyle-check.mjs 4
  EXPECT: ASTYLE OK
  EVIDENCE: automatic-evidence=v1; definition-sha256=99ff22e9222a28dc97cb62774f3845139fe476d3f8d8d1205a555888891a6bfe; exit=0; EXPECT=matched; output-sha256=698c33447599ae53b305f94b371fc54a303379ec7bc6f659076c33398c781efb; output-bytes=18; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G10: the DOS docs check passes, and it now requires the docs to describe OPENMPT.DXE
  CHECK: sh backends/platform/dos/tests/check-docs.sh && grep -c "OPENMPT.DXE" backends/platform/dos/tests/check-docs.sh
  EXPECT: /DOCS OK\n[1-9]/
  EVIDENCE: automatic-evidence=v1; definition-sha256=4fb5b797eb53e44045cef237da24bb8ab83dd8f9d8c050fc04879c94759f41e1; exit=0; EXPECT=matched; output-sha256=3ac0a3dcae77708f383c074e6be2e66d4b8f522fe16506b97b70e36d37eb05bd; output-bytes=10; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G11: the uncommitted .gitignore change from before this work is unchanged, and nothing outside the owned files changed
  CHECK: sh .unlazy/_checks/openmpt-check.sh scope
  EXPECT: OPENMPT SCOPE OK
  EVIDENCE: automatic-evidence=v1; definition-sha256=974243aecb82dc6809a684cd9fd824a92ec08cc02486b552229cb7a4f8c9cf66; exit=0; EXPECT=matched; output-sha256=9bb74517b98218c5605732025333bbbaca53a356ebbcbc4c3e053774e49d3676; output-bytes=17; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G12: the diff re-read against ScummVM's conventions and AI-GUIDELINES.md (short comments, nothing outside the request, no commit made)
  EVIDENCE: manual, 2026-11-25, whole `git diff` and the new files read. No file outside backends/platform/dos and backends/plugins/dos changed (universaltracker.cpp untouched: the program defines the six libopenmpt functions it calls and passes them on). dos.mk takes -lopenmpt off LIBS and links it into openmpt.dxe (dxe-link.sh got -E and passes -L/-l through; engine plug-ins link as before, plugall 112 OK). Load at engineInit() for ags/sludge/testbed (list checked against configure.engine), lazy load on a first tune otherwise, unload from DOSPlugin::unloadPlugin() only with no module open; quit() leaves through _exit(), so no atexit handler into the freed module runs. Provider skips OPENMPT.DXE (FilePluginProvider also scans "."). Static builds unchanged. Tabs, one-line comments, existing naming; astyle clean. Pre-existing check-builds.sh faults (stripped dist compare, SDL markers, testbed regex) noted, not changed. No commit made.

## Follow-up: check-builds.sh faults and the missing `mpt` test

- [x] G13: check-builds.sh passes on all five builds (dist compared with a stripped copy, markers per backend, tracker engines from configure.engine, sources counted only where the build uses them)
  CHECK: BUILDS="stable stable-sdl testbed sdl-testbed stable-static" sh backends/platform/dos/tests/check-builds.sh
  EXPECT: /testbed \(own backend\): current, with libopenmpt in OPENMPT\.DXE[\s\S]*BUILDS OK/
  EVIDENCE: automatic-evidence=v1; definition-sha256=9a6de2ea29a4807f34d86b368e8cfeb3a8f66bcaecf6418963fc7410aa8b969a; exit=0; EXPECT=matched; output-sha256=66f50612c8f6a89251839de8283309365905651fce755f7e35dcba440dde96fa; output-bytes=369; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G14: check-builds.sh still fails on a broken build (negative control: wrong dist exe, no dist OPENMPT.DXE, a stale source)
  CHECK: sh .unlazy/_checks/check-builds-negative.sh
  EXPECT: CHECK-BUILDS NEGATIVE OK
  EVIDENCE: automatic-evidence=v1; definition-sha256=27a2db86332c042929be3b77557ae6bb520ee46e494d16cd9533cf62f2c1ffd1; exit=0; EXPECT=matched; output-sha256=ae586035282636ff453aa69d8d15926511c165025fcb9680e9ba6523bdbcc98c; output-bytes=25; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G15: run-dos-test.sh mpt plays a MOD, an S3M, an XM and an IT file through ScummVM's tracker stream (OPENMPT.DXE) with sound
  CHECK: sh backends/platform/dos/tests/run-dos-test.sh mpt
  EXPECT: /DOSTEST mpt OK .*MOD.*S3M.*XM.*IT/
  EVIDENCE: automatic-evidence=v1; definition-sha256=860de195cb74ee9e3842ff5e6cf427ede0c5ceac11ca10a1da3281de63f6a1f8; exit=0; EXPECT=matched; output-sha256=c919609c8ec52500d8a8e141f7d4203da95dad88c6d3bb0f9029f6ab584d016d; output-bytes=145; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G16: every `run-dos-test.sh <name>` the DOS docs name is a test that exists (check-docs enforces it), and the docs check passes
  CHECK: sh backends/platform/dos/tests/check-docs.sh && grep -c "name a runtime test that does not exist" backends/platform/dos/tests/check-docs.sh
  EXPECT: /DOCS OK\n[1-9]/
  EVIDENCE: automatic-evidence=v1; definition-sha256=747008ecbcd4b71aca5edebc62bcd5536e569562042ffa8de475f2449e81648e; exit=0; EXPECT=matched; output-sha256=098dcb09136400695702dc7b5f01b70ed33473403b03493892d33b13d71a0956; output-bytes=10; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries

- [x] G17: changed source lines are astyle-clean, and the earlier runtime tests plus mptdxe/mptdxesdl still pass
  CHECK: node .unlazy/_checks/astyle-check.mjs 4 && sh backends/platform/dos/tests/run-dos-test.sh mptdxe && sh backends/platform/dos/tests/run-dos-test.sh mptdxesdl
  EXPECT: /ASTYLE OK[\s\S]*DOSTEST mptdxe OK[\s\S]*DOSTEST mptdxesdl OK/
  EVIDENCE: automatic-evidence=v1; definition-sha256=9bc57a9f5bd31161c9e5a404050db8bdcb7f6937c1ffb436fb7db07478157069; exit=0; EXPECT=matched; output-sha256=fd6051ed34ffd61aab44abedc13bbdf5641d1f327f8829941d55703928dac28b; output-bytes=271; shell=/bin/sh; cwd=/Users/eric.perez.jas/Documents/GitHub/scummvm-dos; path=978d9c4384a7/18 entries
