# MS-DOS port: engine lists and build profiles

The lists live in `engines.conf` (same folder). `configure-dos.sh` reads it, so every build of the port uses the same ones. The sections below describe the `stable` list (the 135 engines) first, then the build profiles.

## How it was chosen

Start from the engines ScummVM itself builds by default, then drop only what a DOS machine cannot run. Of the 154 engines (sub-engines counted separately):

| Group | Engines | In |
|---|---|---|
| Built by default upstream, runnable on DOS | 135 | `stable` |
| Marked unfinished upstream | not built | `EXTRA_ENGINES` adds any |
| Not runnable on DOS | 4 | never |

Audit of the engine sources: no engine calls DirectX, or Windows or Mac drivers directly. The only such code is in the bundled libraries of Hpl1, which is excluded for other reasons. The real blockers are OpenGL and Dear ImGui.

### Excluded (cannot run on DOS)

| Engine | Why |
|---|---|
| Hpl1 | needs OpenGL with shaders, and threads |
| Thimbleweed Park (`twp`) | needs OpenGL with shaders |
| The Watchmaker (`watchmaker`) | needs classic OpenGL (and is unfinished upstream) |
| Macs2 | needs the Dear ImGui debugger, which has no DOS display backend |

High colour (15/16-bit) and software 3D (TinyGL) are not exclusions: DOS machines can do both. TinyGL is ScummVM's own software renderer and needs no OpenGL.

## What the 133 engines need

### 1. Run on the current 256-colour screen driver (91)

3 Skulls of the Toltecs, Access, AGI, AGOS, AGOS 2 games, American Laser Games, Another World, Beavis and Butthead in Virtual Stupidity, Beneath a Steel Sky, Blue Force, Bolt, Broken Sword, Broken Sword II, Bud Tucker in Double Trouble, CGE, CGE2, Chamber, Chewy: Esc from F5, Cinematique evo 1, Cinematique evo 2, Cryo Omni3D games, Darkseed, Dragon History, Drascula: The Vampire Strikes Back, Dreamweb, Dungeon Master, Dynamix Game Development System, Eagle Eye Mysteries, Escape From Hell, Eye of the Beholder, Flight of the Amazon Queen, FoxTail, Gamos, Gobli*ns, Got, Groovie, Hades Challenge, Harvester, HE71+ games, HeroCraft, Hugo Trilogy, Hypnotix Inc., IHNM, Illusions Engine, Kingdom: The Far Reaches, Kyra, Labyrinth of Time, Lands of Lore, Little Big Adventure, Lord Avalot d'Argent, Lure of the Temptress, M4, Macromedia Director, MacVenture, MADE, MADS, Magic Composer, Might and Magic, Might and Magic 1, Might and Magic Xeen, Mission Supernova, Mohawk, Mortevielle, Myst, Neverhood, Parallaction, Pink Panther, Plumbers Don't Wear Ties, Private Eye, Return to Ringworld, Ringworld: Revenge of the Patriarch, SAGA, SAGA2, Sanitarium, SCI, SCI32 games, SCUMM, Teen Agent, The Fool's Errand, The Lost Files of Sherlock Holmes, The Prince and The Coward, Tinsel, Toonstruck, Tot, Touche: The Adventures of the Fifth Musketeer, TsAGE, Ultima, v7 & v8 games, Versailles 1685, Voyeur, WAGE

### 2. Need high colour (20)

These compile in, but the DOS screen driver is 256-colour only, so each stops at start-up with an unsupported-colour-mode error until the driver gets 15/16-bit VESA modes.

ADL, Adventure Game Studio, Blade Runner, Blazing Dragons, Hopkins FBI, Hyperspace Delivery Boy!, mTropolis, Nancy Drew, Nikita Game Interface, Riven: The Sequel to Myst, The Griffon Legend, The Journeyman Project: Pegasus Prime, The Last Express, Tony Tough and the Night of Roasted Moths, Trecision Adventure Module, UFOs, Ultima IV - Quest of the Avatar, Ultima VI = The False Prophet, Ultima VIII - Pagan, V-Cruise

### 3. Need high colour and software 3D (3)

Freescape, Grim, Wintermute3D. TinyGL runs in plain C, so it works on DOS, but it asks a lot of the CPU.

### 4. Need an image, font or video library (19)

These need portable C libraries, which are cross-built for DJGPP (`deps/build-all.sh`) into the prefix that `DEPS` names (see `dos-env.sh`): zlib, libpng, libjpeg, giflib, FreeType, libmpeg2 and libtheora. `configure-dos.sh` uses a library when it is there. If one is missing, `configure` skips the engines that need it, with a warning that names the library.

| Engine | Library | Also needs |
|---|---|---|
| Alcachofa | mpeg2 | software 3D |
| Alfred Pelrock | png |  |
| BAGEL | freetype2 | high colour |
| Broken Sword 2.5 | png, theoradec | high colour |
| CRAB | freetype2, png | high colour |
| Glk Interactive Fiction games | freetype2, jpeg, png | high colour |
| Groovie 2 games | jpeg | high colour |
| Myst 3 | jpeg | software 3D |
| Myst ME | jpeg | high colour |
| Phoenix VR | gif | high colour |
| QD Engine | mpeg2 | high colour |
| Red Comrades | freetype2 | high colour |
| Sludge | png | high colour |
| Starship Titanic | jpeg | high colour |
| Tetraedge | freetype2, jpeg, png, theoradec | software 3D |
| The Journeyman Project 2: Buried in Time | freetype2 |  |
| The Longest Journey | freetype2 | software 3D |
| Wintermute | jpeg, png | high colour |
| Z-Vision | freetype2 | high colour |

### Unfinished upstream: not built

The engines ScummVM does not build by default are left out, as in the other ports. `EXTRA_ENGINES="startrek colony"` (in a build folder of its own) adds any of them.

## What the full default list costs (measured 2026-11-09)

All 133 engines compile and link for DJGPP into one program. The build starts in DOSBox-X: the 1024x768 menu comes up and `--list-engines` names them all. No game of the new engines was run, since no data was at hand.

| | File on disk | Loaded into RAM | RAM to reach the menu |
|---|---|---|---|
| Default list, 133 engines | 117 MB (89 MB with the debug information stripped) | 88.9 MB | about 97 MB |
| Small build: SCUMM, AGI, SCI, Mohawk, Groovie | 15.6 MB | 11.6 MB | about 19 MB |

DOS loads the whole program into memory before it starts, so in these builds every engine costs RAM whether or not its game is played. (The engines are plug-ins loaded one at a time by default, which takes the full list from about 110 MB at the launcher down to 32 MB; the figures in this table are for the all-in-one build made with `DOS_STATIC=1`. See `PLUGINS.md`.) The engines add up to 77.5 MB of the 88.9 MB, an average of 0.7 MB each. The largest: Adventure Game Studio 4.7 MB, Director 4.2, Glk 3.3, SCUMM 3.2, MADS 2.6, Ultima 2.6, TsAGE 2.3, Kyra 2.2, M4 2.1, SCI 2.1, Might and Magic 2.1, Nancy Drew 2.1.

So the full list needs a machine with at least 128 MB of RAM. On a smaller one, build with only the engines whose games are installed: run `configure-dos.sh` with `ENGINES_ONLY` set to your own engine list (see below), and the program shrinks by about 0.7 MB per engine dropped.

## DJGPP fixes the full list needed

- Hypno: DJGPP's `<math.h>` defines `PI`, which is also a token name in the grammar. `hypno.h` removes the macro.
- Ultima (Nuvie): no `nearbyint()` in DJGPP's library, so `nuvie_defs.h` maps it to `rint()`.
- Adventure Game Studio (alfont): no `wcslen()`, so `alfont.cpp` defines one.

## The build profile

`configure-dos.sh` (same folder) configures a build for the one profile. It can be run from any folder. The build goes in `build-dos-<profile>` in the source folder (`build-dos-<profile>-static` with `DOS_STATIC=1`), the way the Atari scripts keep a `build-<variant>` folder, or in the folder given as a second argument; `/build*` in `.gitignore` keeps it out of git status. It finds the DJGPP cross-compiler through `DJGPP_HOME` (or `PATH`) and the cross-built libraries through `DEPS`; `dos-env.sh` says how. Every profile is made with the engines as plug-ins unless `DOS_STATIC=1` is set.

| Profile | Engines | Colour | File | Stripped | Loaded into RAM | RAM to reach the menu |
|---|---|---|---|---|---|---|
| `stable` | the 135 above: no beta or unfinished engines | high colour | 117.0 MB | 88.9 MB | 88.9 MB | about 97 MB |

(Measured 2026-11-09; MB here means 1,048,576 bytes. "Stripped" is the file with the debug information taken out, which is the part DOS actually loads.) It starts in DOSBox-X and brings up the menu. For a small machine, build with only the engines whose games are installed.

The build to ship is `stable`, with the engines as plug-ins (the `stable` test build; `stable-static` is the all-in-one control).

```
backends/platform/dos/configure-dos.sh stable
make -C build-dos-stable -j8
```

Settings for `configure-dos.sh`: `ENGINES_ONLY="scumm"` builds only the engines you name, whatever the profile says (`ENGINES_ONLY=scumm configure-dos.sh stable build-dos-scumm06` does this: SCUMM version 0 to 6 only, which includes Sam & Max, Monkey Island 1 and 2 and Day of the Tentacle; `scumm_7_8` adds The Dig, Full Throttle and Curse of Monkey Island, and `he` the Humongous games); `EXTRA_ENGINES="testbed"` adds engines to any profile; `ENGINES_ONLY` and `EXTRA_ENGINES` need the build folder as the second argument, so that they cannot reconfigure the profile's own folder into something else.

## High colour (15 and 16 bit)

Built into `stable` (switched on in `configure` for the DOS backend, `--disable-16bit` leaves it out). When an engine asks for a 16-bit pixel format, the driver switches to a VESA mode laid out the same way (RGB565 or RGB555) and shows the game there. It offers the formats the card has, 565 ahead of 555, then 256 colours.

- **Mode choice:** a mode of exactly the game's size if the card has one; otherwise the smallest mode that holds the game doubled in each direction (a 320x200 game fills a 640x400 mode); otherwise the smallest mode that holds it, with the picture centred.
- **Cursor:** palette cursors and 16-bit cursors both work.
- **When the card cannot show the game in high colour** (it has no high colour mode, or none big enough: 512 KB of video memory holds 640x400 at 16 bits but not 640x480), the game is shown in 256 colours instead, dithered (`dos-dither.h`). The screen gets a fixed palette, a cube of 6 x 7 x 6 colours. Each game pixel goes through three small tables, one per colour channel, that add a repeating 4x4 pattern (a Bayer matrix, tied to the screen position, so nothing shimmers) before rounding to the nearest level; the three results add up to the palette index. A 4x4 block averages to the true colour to within 2 of 255, where plain rounding can be off by 27. The mouse cursor uses plain nearest-colour rounding, so it stays crisp. The mode used is VGA 13h for a game up to 320x200, otherwise the smallest 256 colour VESA mode that holds the game, which is centred in it. The in-game menu uses the usual 3-3-2 palette while it is up. High colour formats are always accepted (listed after 256 colours), so engines that need a 16-bit format start on any card.
- **Menus:** the launcher uses a 16-bit mode when the card has one at the menu's size, so the theme is shown in its own colours instead of the 3-3-2 approximation. A card without one (for example 800x600 with 512 KB of video memory) keeps the 256-colour menu.
- **Switches** (in `scummvm.ini`, or as environment variables for testing): `dos_16bit=false` / `SCUMMVM_DOS_16BIT=0` turns the card's high colour modes off (high colour games then run dithered in 256 colours); `dos_16bit_fallback=false` / `SCUMMVM_DOS_DITHER=0` turns the 256 colour fallback off (a game that does not fit a high colour mode is then refused); `SCUMMVM_DOS_DITHER=force` uses the fallback even where the card has the modes; `dos_gui_depth=8|16|auto` / `SCUMMVM_DOS_GUI_DEPTH` sets the menu's depth.
- **Tests:** `SCUMMVM_DOS_SELFTEST16=1` has the driver do what an engine would (each format, 320x200 and 640x480, a pattern and a cursor, screenshots) and stop; `SCUMMVM_DOS_VERIFY=1` compares video memory with what should be there after every update; `SCUMMVM_DOS_DIRECT_MINW=<width>` and `SCUMMVM_DOS_DIRECT_MAXW=<width>` hide narrower or wider high colour modes (to try a card without the small ones, or without the big ones, as with little video memory); `SCUMMVM_DOS_BANKED=1` forces the slower access used by cards without a linear framebuffer.
- **Verified:** in DOSBox-X, 565 and 555 at 320x200 and 640x480, doubled and centred, linear and banked, 16-bit menu, a 256-colour game started from the 16-bit menu and back; the 256 colour fallback forced, as a real consequence of a card with no mode wider than 320, with the fallback off (refused cleanly), and with high colour off; the dithering maths on the host. **Not verified:** a real 16-bit game, and any real card.
