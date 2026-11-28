# DOS port: features compared with the other ScummVM builds

What the DOS builds (stable, all; see `ENGINES.md`) have and lack next to a normal
desktop build, and what was done about it. Compiled from ScummVM's `configure` (every
`add_feature` / `add_component` and library probe), the `OSystem::Feature` list and the
backend managers, checked against what `configure-dos.sh` switches on (`config.h` of the
`all` build) and what `backends/platform/dos/` implements.

Legend: **same** = as other builds, **new** = added in the parity work (2026-11-09),
**opt-in** = built but off until asked for, **no** = impossible on DOS, **not yet** = possible, not done.

## Audio

| Feature | Other builds | DOS builds | |
|---|---|---|---|
| PCM sound output | SDL audio | Sound Blaster / Pro / 16 through DMA, 22 kHz 16-bit stereo, silence without a card | same |
| FM music | OPL emulators (DOSBox, MAME, Nuked) | the card's real OPL2 / dual OPL2 / OPL3 chip (`dos-opl.cpp`), DOSBox and MAME software emulators as fallback | same, better |
| Nuked OPL (accurate emulator) | yes | left out: far too slow for a Pentium | no |
| MIDI output | ALSA, CoreMIDI, WinMM, ... | MPU-401 (`dos-midi.cpp`): Roland MT-32 / General MIDI | same |
| Software MT-32 (Munt) | yes | built only with `DOS_MT32EMU=1` (needs a Pentium II or faster) | opt-in |
| FluidSynth / FluidLite (SoundFonts) | yes | no DOS port; far too slow for a Pentium | no |
| TiMidity++ server, Android SONiVOX, OPL2LPT, RetroWave | yes | not applicable to a DOS machine | no |
| MP3 (libmad), Ogg (Tremor), FLAC | yes | yes (all profiles) | same |
| AAC (libfaad), AC-3 (liba52), Musepack (libmpcdec) | yes | **new**: cross-built (`deps/build-audio-libs.sh`) and switched on in stable and all | new |
| Module music: libmikmod, libopenmpt | yes | **new**: libopenmpt 0.8.9 (`deps/build-extra-libs.sh openmpt`), in every build that has an engine that plays modules (stable and all; ScummVM's `configure` leaves it out of a build without one), which makes every format its loaders read playable (MOD, S3M, XM, IT, MPTM, MTM, 669, AMF, DBM, MED, OKT, PSM, ULT and the rest), for the AGS games with module music and for every SLUDGE game (`DOS_NO_OPENMPT=1` leaves it out). In a plug-in build (the default) it is not in the program but in `OPENMPT.DXE` next to it, loaded only while a game of one of those engines runs (`PLUGINS.md`, `tests/run-dos-test.sh mptdxe`). Tested under DOS with a MOD, an S3M, an XM and an IT file, played through ScummVM's tracker stream (`tests/run-dos-test.sh mpt`); the other formats share the same code path and were not tried one by one. ScummVM's own tracker stream (`audio/mods/universaltracker.cpp`) had two faults that silenced MOD and XM files and dropped the last bytes of a file; both are fixed (see below) libmikmod is not built: libopenmpt covers its formats and ScummVM allows only one of the two | new |
| Audio CD (original game CDs) | digital rips only on current SDL builds | **new**: real CD audio through MSCDEX (`dos-cdaudio.cpp`): ScummVM plays tracks of a music CD in the CD-ROM drive (INT 2Fh AX=1510h: the table of contents, play with a start and a length, stop, volume and balance, a track played several times or for ever). The start of a play is an HSG sector, the addressing every MSCDEX driver must take (Red Book addressing is optional for a driver); a request MSCDEX itself refuses (carry set) counts as failed; the volume is set through 0, as some drivers pass on only a change. Digital rips (`track1.ogg`...) are still preferred when the game has them, as on other systems. With no MSCDEX or no CD drive the program writes one WARNING to the log and plays the files only. ScummVM counts a disc's tracks from 0, as its other CD managers do (SDL, Windows, macOS): track 0 is the disc's first track, so on a game CD with a data track first, the first music track is 1, which is what the engines ask for. The `cdrom` setting picks the drive (`0` is the first CD-ROM drive, or a letter like `D:`). The log says which drive plays, its driver's device status (bit 4: it plays audio) and, at the first play, which disc track. Games with CD audio (SCUMM's Monkey Island CD, The 7th Guest, ...) ask for the "tracks need to be ripped" dialog before they open the CD; the DOS CD manager answers `isAudioDiscInDrive()` (a new call of `AudioCDManager`, false on other systems) with whether the drive holds a disc with music tracks, and with one in, the engine writes a WARNING to the log instead of showing the dialog (with no disc and no rips the dialog shows, as everywhere). The drive plays into the sound card's CD input through its own audio cable, not through ScummVM's mixer; a Sound Blaster 16 keeps that input at 0 (silent) until a mixer program sets it, so when CD audio first plays from the drive and `BLASTER` names an SB16 (T6, or an H channel) or an SB Pro (T2, T4, T5), a silent CD input is raised to the voice's level (SB16: registers 0x36 and 0x37, and the CD switches of 0x3C if both are off; SB Pro: 0x28) and put back when the CD manager closes or ScummVM quits (which also stops the drive); a level the user set is left alone. With still no music: headphones in the drive's front headphone socket hear the drive itself (music there and none from the speakers is the audio cable or the card's CD input; none there either is the driver or the drive, see the log). Tested against DOSBox-X's MSCDEX with a CD image of three audio tracks (`tests/run-dos-test.sh cd`: the table of contents, plays, the drive's position after a play, repeats, the dialog skipped, a silent SB16 CD input raised and put back) | new |
| Text to speech | yes | no | no |

## Video and graphics

| Feature | Other builds | DOS builds | |
|---|---|---|---|
| 8-bit palette graphics | yes | VGA mode 13h and VBE 2.0, linear framebuffer or banked | same |
| High colour (16-bit) | yes | VBE direct-colour modes; dithered 256-colour fallback when the card's memory is too small | same |
| Menu resolution | window size | the monitor's native size from EDID / VBE, up to 1024x768; neither the menus nor a game get a mode larger than the monitor reports or than the card's video memory holds (640x480 and smaller, which every VGA monitor shows, are only held to the memory) | same |
| Aspect ratio correction | option | inherent: 320x200 games use the 4:3 mode and the monitor stretches it | same |
| OpenGL, OpenGL ES, shaders | yes | no (no GPU API under DOS); the software 3D renderer TinyGL is built in | no |
| Software scalers (2x/3x, hq2x, edge), filtering, stretch modes | Graphics options | own backend: not offered, the game is shown 1:1 (doubled when small and high colour) and the monitor scales it; SDL3 build: scalers only (see the Graphics options tab below) | not yet |
| Games larger than 640x480 | yes | **new**: the smallest 256 colour VESA mode that holds them, the game centred in it (SCUMM's Hercules mode, 720x350, gets 800x600; it used to fail with "Could not switch to resolution") | new |
| Fullscreen / window | option | always full screen | no |
| PNG, JPEG, GIF pictures; FreeType fonts | yes | stable and all | same |
| FriBidi (right-to-left text) | yes | **new**: stable and all (Arabic, Hebrew, Persian: the launcher mirrors and the text runs right to left; tried with Hebrew) | new |
| Theora, MPEG-2, Bink, Indeo, QuickTime, Smacker video | yes | yes (stable and all) | same |
| VP8 / VP9 (libvpx) | yes | left out: only SLUDGE games' cutscenes use it; they are skipped (with a log line) and the game goes on | not yet |
| Screenshots (Alt+S) | PNG | **new**: Alt+S saves `SHOT0001.BMP`, `SHOT0002.BMP`, ... in the current folder (BMP, not PNG) | new |

## The Graphics options tab

The tab offers only what changes something on DOS. What is not offered is not shown, so the rows that are left move
up (the controls ScummVM leaves out when a backend lacks a feature, and on DOS also the Graphics mode list, which has
one entry or none, and the Shader controls).

| Control | `--backend=dos` | `--backend=sdl` |
|---|---|---|
| Graphics mode | not shown (no modes to choose) | not shown (one: SDL Surface; the build has no OpenGL) |
| Render mode | yes (CGA, EGA, Hercules... as each game allows) | yes |
| Rotation mode, Stretch mode | not shown | not shown: the picture is always 1:1 and centred, whatever `rotation_mode` and `stretch_mode` say |
| Scaler | not shown | yes (Normal, AdvMame, HQ, Edge, SaI...; 2x on a 320x200 game needs a 640x400 or 640x480 16-bit mode) |
| Shader | not shown | not shown |
| Aspect ratio correction | not shown (13h is stretched to 4:3 by the monitor) | yes (200 lines drawn as 240 in software) |
| Fullscreen mode | ticked and greyed out: always full screen | ticked and greyed out: always full screen (it used to be shown unticked and did nothing) |
| Filter graphics | not shown | not shown: with no renderer it did nothing (`filtering` is ignored) |
| V-Sync | not shown (the picture is shown at the vertical retrace) | yes, on by default: SDL's driver waits for the vertical retrace before it copies the picture or flips pages (`vsync=false` turns it off) |
| Game 3D Renderer | yes (Software: TinyGL) | yes (Software: TinyGL) |

## Input

| Feature | Other builds | DOS builds | |
|---|---|---|---|
| Mouse | yes | INT 33h driver | same |
| Mouse wheel | yes | **new**: drivers with the wheel API (CuteMouse 1.9 and later) | new |
| Keyboard | yes | IRQ 1 hook, press and release | same |
| Other keyboard layouts | yes (the system's) | **new**: the layout loaded with DOS's `KEYB` is followed (tried with German; the unit tests also cover French AZERTY, accented letters, shifted symbols and AltGr). KEYB turns scan codes into the characters of the active code page and puts them in the BIOS keyboard buffer; the program pairs each key press with that character by its scan code and converts it from the code page (437, 737, 775, 850, 852, 855, 857, 858, 860, 861, 862, 863, 865, 866 and 869 are built in; the page is asked of DOS, INT 21h AX=6601h) to Unicode for the text the engines and the launcher see. AltGr (the right Alt key) types its characters, and a dead key waits for the next key. Key codes follow the layout too (on AZERTY, the key that types `a` is `a`). Without a loaded layout it is the US one, as before. Tested with `KEYB GR` under DOSBox-X (`tests/run-dos-test.sh kbd`) and by unit tests (`test/backends/dos/keymap.h`); not yet tried on the real keyboard | new |
| Joystick / gamepad | yes | **new**: game port (port 0x201) stick moves the cursor, two buttons click; off unless `SCUMMVM_DOS_JOYSTICK=1` is set, and not yet tried on real hardware | opt-in |
| Touch screen, on-screen controls, virtual keyboard | yes | no | no |

## User interface and system

| Feature | Other builds | DOS builds | |
|---|---|---|---|
| Translated user interface (`translations.dat`) | yes | **new**: stable and all (`DOS_NO_TRANSLATION=1` to leave it out); the language follows the country set in CONFIG.SYS, or `gui_language` | new |
| Themes (Classic, Modern, Remastered) | yes | yes (8.3 file names) | same |
| Engines' data files (`kyra.dat`, `queen.tbl`, `sky.cpt`, ...) | next to the program | **new**: found under their 8.3 names (the program looks for `CLASSICM.DAT` when asked for `classicmacfonts.dat`); `make-dist.sh` assembles them | new |
| Long file names | yes | only with an LFN driver (DOSLFN, Windows 9x DOS box); otherwise 8.3 | no |
| Plug-ins loaded at run time | yes | **new, the default** (`DOS_STATIC=1` links the engines into the program instead): the engines and the detection tables are DXE3 modules in a `PLUGINS` folder, loaded one at a time with DJGPP's `dlopen`, so the RAM to reach the launcher no longer grows with the number of engines: 32 MB for all 112 engines as plug-ins against 110 MB with them inside the program. Every engine plug-in loads and unloads, Sam & Max runs through the SCUMM plug-in; tried under DOSBox-X and on real hardware (Pentium 200 MMX, FreeDOS with JEMMEX and the DPMI host that comes with FreeDOS). See `PLUGINS.md` | the default |
| Savegames, config file, log file | yes | yes (`SCUMMVM.INI`, saves with time stamps; `SCUMMVM.LOG` as on the other platforms: every message with a time stamp, new at each run, the `logfile` setting honoured; while the screen is in a graphics mode nothing is printed over it, and the last error is printed on leaving) | same |
| Debugger | console and Dear ImGui | the in-game console; ImGui needs OpenGL or SDL | partly |
| Local web server (launcher's LAN tab: browse, upload and download files and saves from a browser) | yes | **new**: stable and all, over the Watt-32 TCP/IP library and a packet driver (see `NETWORK.md`). Without a packet driver the program writes one WARNING to the log and the LAN tab is not offered | new |
| Cloud saves, ENet multiplayer, DLC and icon-pack downloads, update check | yes | no HTTPS (no TLS library for DOS) and no use for the rest | no |
| Discord presence, taskbar, dock, system file dialogs, open URL | yes | not applicable | no |
| Event recorder, virtual keyboard builds | off by default everywhere | same | same |

## Engines

See `ENGINES.md`. Three engines cannot run on DOS and are left out: Hpl1 and Thimbleweed
Park (both need OpenGL with shaders) and The Watchmaker (classic OpenGL). Freescape, Grim and
Wintermute 3D use the software renderer TinyGL, which works on DOS but asks a lot of the CPU.
Every other engine is in the stable or all profile.

## Speed notes (found with Blade Runner on a Pentium 200)

* **Adding a game** used to run the detection of every engine ScummVM knows, built in or not (`configure`'s
  `--detection-full`, on by default): about a hundred modules, several of which open and read every file of the
  folder (Director, AGS, WAGE). The DOS profiles now pass `--disable-detection-full`, so only the engines that are
  in the build detect. Scanning a Blade Runner-sized folder (263 files) fell from about 10 s to under 1 s in
  DOSBox-X, and the exe is 4 to 6 MB smaller. A game of an engine that is not in the build is now reported as unknown rather than as
  "known, but not built in".
* **16-bit games on a 256-colour screen** (the dithered fallback): the per-pixel loop works four pixels at a time with
  the pattern offsets constant. Byte-identical output (tested on 40,000 random rows and on the real program's
  test images).
* **Sound**: `output_rate=11025` in the `[scummvm]` section of `SCUMMVM.INI` halves the mixer's work (duller treble); `sb_bits=8` plays
  8-bit mono. Both existed before; neither is the cause of Blade Runner's gaps.

## A fix in ScummVM's own tracker player

Found by the DOS tracker test, which played a MOD, an S3M, an XM and an IT file through the program's stream: only the S3M
and the IT file made sound. `audio/mods/universaltracker.cpp` (libopenmpt version, used by SLUDGE and AGS on every platform
that builds it) had two faults:

* its "no more data" answer was whether the *file* had been read to the end. For the MOD and XM test files that was already
  so once loading was done (the loader's last read ran into the end of the file), so the mixer saw a stream that was over
  before the first sample and played nothing; the S3M and IT files happened not to. It now answers whether the *song* is
  over (libopenmpt gave fewer samples than asked for), and rewinding clears that. This is the fault the test showed.
* the read callback returned 0 when libopenmpt asked for more bytes than were left in the file, which throws those bytes
  away (a MOD keeps its sample data at the end of the file). It now returns what was read, as libopenmpt expects. Found by
  reading the code; the test files do not show a difference, so it is not covered by a test of its own.

## Tests

* **Unit tests** (`make test` in a native build tree; the DOS port's are in `test/backends/dos/`): the parts of the port
  that are arithmetic and byte layouts are kept in headers with no DOS calls in them, so they run on any machine:
  keyboard layouts (`dos-keymap.h`, `dos-bioskey.h`, `dos-codepages.h`), scan codes and key codes (`dos-scancodes.h`),
  CD audio (`dos-cd.h`), the BLASTER variable and the DSP's sample rates (`dos-blaster.h`), where the
  Sound Blaster's DMA has got to and the mixer's clock without a card (`dos-dmaclock.h`), dithering and the overlay
  palette (`dos-dither.h`, including a check that the unrolled line converter gives the same bytes as the plain one),
  VESA mode information, display pages and banks (`dos-vesainfo.h`), video mode choice (`dos-modes.h`), 8.3 names and
  country languages (`dos-names.h`), dotted addresses (`dos-netparse.h`), the joystick (`dos-joystick.h`), the start-up
  banner (`dos-banner.h`) and file and savegame lookups (`dos-filesys.h`).
* **Runtime tests under DOSBox-X** (`backends/platform/dos/tests/run-dos-tests.sh`): engine data found by long names,
  keyboard layout (`KEYB GR`), libopenmpt on four module formats, ScummVM's tracker stream, CD audio from a CD image,
  no CD drive, the web server over an emulated network card, and the launcher/a game coming up. See `tests/README.md`.

## SDL3 backend (`configure --backend=sdl`, `DOS_BACKEND=sdl`)

`configure-dos.sh` builds `--backend=sdl` by default: ScummVM's ordinary SDL backend on SDL3's DOS driver, which
needs SDL3 built for DJGPP (`deps/build-sdl3.sh`, part of `deps/build-all.sh`; SDL3 3.4 has no DOS driver, so it is
a snapshot of SDL's main branch). Build folder: `build-dos-<profile>-sdl`. `DOS_BACKEND=dos configure-dos.sh stable`
builds `--backend=dos` instead (the port's own VGA/VESA, keyboard, mouse and Sound Blaster code, which plain
`configure` still chooses for DJGPP), in `build-dos-<profile>`.

State: the launcher comes up and draws under DOSBox-X on SDL3's VESA video driver and Sound Blaster audio driver, and the
program is left cleanly (`tests/run-dos-test.sh sdl`, on a Testbed-only build). Also tried on real hardware.

| Part | `--backend=dos` | `--backend=sdl` |
|---|---|---|
| Video | `dos-graphics.cpp`, `dos-vesa.cpp` | SDL3: VGA 13h, VBE 1.2+ |
| Keyboard, mouse, joystick | `dos-input.cpp`, `dos-keyboard.cpp` | SDL3 |
| PCM sound | `dos-sb.cpp`, `dos-mixer.cpp` | SDL3: Sound Blaster, Pro, 16 from `BLASTER` |
| FM music (OPL2 / OPL3 chip) | `dos-opl.cpp` | same code |
| MIDI (MPU-401) | `dos-midi.cpp` | same code |
| Audio CD (MSCDEX) | `dos-cdaudio.cpp` | same code |
| 8.3 names, data folders, saves | `dos-files.cpp` | same code |
| Local web server (Watt-32) | `dos-net.cpp` | same code |
| Plug-ins (DXE3) | `backends/plugins/dos` | same code |
| Entry point, `OSystem` | `dos.cpp` | `dos-sdl.cpp`, `dos-sdl-main.cpp` (an `OSystem_SDL`) |

What the SDL3 build gives up or has not got, and is not done for now:

* **VBE 2.0.** SDL3 supports VBE 1.2+ and keeps its own mode list; the port picks from it, leaving out the modes
  larger than the monitor (EDID) or the video memory, as the own backend does.
* **The dithered 256-colour fallback** for cards with too little video memory, and the direct-colour (16-bit) path
  of `dos-graphics.cpp`: what SDL's driver offers is what there is.
* **Page flipping and the mode switching of `dos-graphics.cpp`**, replaced by SDL's own. On a linear framebuffer with
  room for two pages SDL's driver draws to the hidden page and flips; in a banked mode or mode 13h it copies to the
  page on the display. It copies the whole window surface at every update on a linear framebuffer (it ignores dirty
  rectangles there). With V-Sync on (the default, `vsync`) it waits for the vertical retrace first, so the picture
  does not tear.
* **The video mode** is the smallest of SDL's modes that holds the scaled screen, in 16 bits (RGB565 ahead of 5-5-5)
  or else 32 bits. A scale factor whose screen no mode holds is lowered (a non-default scaler on the 800x600 menu, a
  640x480 game with a 2x scaler), and the menus are always 1x at the resolution chosen for them.
  `SCUMMVM_DOS_MODE_MAX=<width>x<height>` hides the larger modes, for testing.
* **`KEYB` layouts, code pages and AltGr** (`dos-keyboard.cpp`, `dos-keymap.h`, `dos-codepages.h`): not used, SDL's
  keyboard driver hooks IRQ 1 itself, so keys are those of a US keyboard.
* **The mouse wheel**, unless SDL's mouse driver gives it.
* **Alt+S screenshots as BMP** (`SHOT0001.BMP`): ScummVM's own screenshot function is used.
* **Cooperative threads.** SDL's DOS threads switch only at `SDL_Delay` and when events are pumped, and its sound
  thread makes one device buffer per switch, so an engine that polls rarely starves the sound (KQ6's speech was
  choppy: about half the sound was made). Two things in `dos-sdl.cpp` carry it: every event poll gives the sound thread
  four turns, enough to refill SDL's ring buffer, and the device buffer is 2048 frames (46 ms, SDL's default is 1024) so
  the card plays about a quarter of a second before it needs more. The price is that the sound the mixer has made
  reaches the speaker up to about a quarter of a second later (lip-sync that follows the mixer's position runs that
  much ahead); `set SDL_AUDIO_DEVICE_SAMPLE_FRAMES=1024` (or 1536) trades some of the margin back. A load of a second
  or so (a new scene) still breaks the sound, as nothing runs the sound thread during it. The MIDI driver yields while it
  waits.
* **Test hooks.** `SCUMMVM_DOS_SHOTS` and `SCUMMVM_DOS_QUITAT` work in both backends; `SCUMMVM_DOS_CLICKS`,
  `SCUMMVM_DOS_RTL`, `SCUMMVM_DOS_SELFTEST16` and the like are in the own backend only.
* **No joystick switch** (`SCUMMVM_DOS_JOYSTICK`): SDL reads the game port itself.
* **Nearptr.** SDL's DOS drivers need the "fat DS" trick (`__djgpp_nearptr_enable()`, called first in `main`), which
  switches off the DPMI host's memory protection.
* **Sample rate.** SDL opens the card at ScummVM's 44.1 kHz; the own backend plays 22 kHz. (`--output-rate=22050` only
  makes SDL convert it back up to 44.1 kHz, and did not change how the sound held up.)
* **OpenGL.** Left out (`--opengl-mode=none`): SDL's DOS driver has no GL, so the OpenGL graphics manager could only
  be tried and fail, and its modes were offered in the Graphics options for nothing.
* **The Dear ImGui debugger** is left out of the build, as it is of the other one.

## What is left

* Software scalers on the own backend (2x nearest or smooth scaling for 320x200 games), and the other Graphics
  options that could work on a Pentium: V-Sync from the page flipping, aspect ratio correction for a 320x200 game
  shown in a VESA mode.
* Real hardware: the joystick and the keyboard layouts were tried only in DOSBox-X. CD audio on a real drive came back
  with no music heard (SOMI, The 7th Guest); the fixes for it (HSG play, the CD input, the dialog) are tested in DOSBox-X only.
* libvpx would restore SLUDGE's cutscenes; it can be cross-built the same way as the other libraries.

## Page flipping

In a VESA mode with room for two screens in the card's video memory, the picture is drawn to the page that is not on the
display and switched in at the vertical retrace, together with the new palette (VBE function 07h). That removes tearing
and pixels shown in the wrong colours. Needs the memory the card reports (see the `DOSGFX: the card reports` line of
the log) and a card that takes the display start function; otherwise the mode is single-buffered as before. VGA mode
13h (320x200) is not flipped. `dos_pageflip=false` or `SCUMMVM_DOS_PAGEFLIP=0` turns it off. Checked under DOSBox-X by
the `pageflip` tests; DOSBox-X does not show tearing, so how it looks on a real card has not been seen.
