# Libraries for the MS-DOS (DJGPP) port of ScummVM

Everything here is built with `i586-pc-msdosdjgpp-gcc` for a 486 (`-march=i486`), as static libraries, into `build-dos-deps/prefix` in the source folder. `dos-env.sh` makes that the default `DEPS`, and `configure-dos.sh` points ScummVM at it (`--with-<lib>-prefix=$DEPS`).

| Library | Version | Used by | Built by |
|---|---|---|---|
| libmad | 0.15.1b | MP3 speech and music | `build-libs.sh` |
| libogg | 1.3.5 | Ogg container | `build-libs.sh` |
| Tremor | master | Ogg Vorbis (integer decoder) | `build-libs.sh` |
| libFLAC | 1.4.3 | FLAC (decoder only) | `build-libs.sh` |
| zlib | 1.3.1 | libpng; ScummVM's own zlib use | `build-image-libs.sh` |
| libpng | 1.6.43 | PNG pictures | `build-image-libs.sh` |
| libjpeg (IJG) | 9f | JPEG pictures | `build-image-libs.sh` |
| giflib | 5.2.2 | GIF pictures | `build-image-libs.sh` |
| FreeType | 2.13.2 | TrueType fonts | `build-image-libs.sh` |
| libmpeg2 | 0.5.1 | MPEG-2 video (decoder only) | `build-image-libs.sh` |
| libtheora | 1.1.1 | Theora video (decoder only) | `build-image-libs.sh` |
| liba52 | 0.7.4 | AC-3 audio (ZVision's Zork DVD audio) | `build-audio-libs.sh` |
| libmpcdec | 1.2.6 | Musepack audio (QD engine) | `build-audio-libs.sh` |
| faad2 (libfaad) | 2.10.1 | AAC audio in QuickTime files (Mohawk, Director, ...) | `build-audio-libs.sh` |
| FriBidi | 1.0.16 | right-to-left text (Arabic, Hebrew, Persian user interface) | `build-extra-libs.sh` |
| Watt-32 | master | TCP/IP and BSD sockets over a packet driver: the local web server. Installed in `prefix/watt32/` (its headers would shadow DJGPP's, so ScummVM takes them in for one file only) | `build-extra-libs.sh` |
| SDL3 | main, snapshot `fda4ece` (3.5.0) | the SDL3 backend (`DOS_BACKEND=sdl`): SDL's DOS video, audio, keyboard, mouse and joystick drivers. A release (3.4.x) has no DOS driver. Needs `cmake` | `build-sdl3.sh` (not part of `build-all.sh`) |
| CWSDPMI | r7 | the DPMI host DJGPP programs start when DOS has none. Not linked: `make-dist.sh` puts the unmodified `CWSDPMI.EXE` from the binary package next to `SCUMMVM.EXE` | `fetch-sources.sh` (nothing to build) |
| libopenmpt | 0.8.9 | tracker music: MOD, S3M, XM, IT and the other module formats (SLUDGE games, AGS games with module music). 8 MB static library | `build-extra-libs.sh openmpt` |

The libraries are static, so they end up inside `SCUMMVM.EXE` and the plug-ins: the `dist/` folder needs none of them at run time (only `CWSDPMI.EXE`, which is a program of its own). What is kept in the repository is the recipe, not the binaries: `sources.conf` (version, licence, URL and sha256 of every package), the build scripts, and the scripts that fetch and check the sources.

## Building them

```
sh backends/platform/dos/deps/build-all.sh     # fetch, check, build everything, collect the licences
```

or step by step:

```
fetch-sources.sh                   # downloads into build-dos-deps/dl, checks the sha256, unpacks into build-dos-deps/src
build-libs.sh                      # libmad, libogg, Tremor, libFLAC     (build-libs.sh libmad: just one)
build-image-libs.sh                # zlib, png, jpeg, gif, freetype, mpeg2, theora
build-audio-libs.sh                # a52, mpcdec, faad   (compiled directly: no configure scripts are run)
build-extra-libs.sh                # fribidi, watt32, openmpt   (same; needs DOSBox-X to make Watt-32's error tables)
collect-licenses.sh                # the licence texts, into build-dos-deps/prefix/licenses
```

Needs the DJGPP cross-compiler (`DJGPP_HOME`, see `../dos-env.sh`), `curl`, `tar` and `unzip`. `DEPS_ROOT` puts the work folder somewhere other than `build-dos-deps` (`/build*` in `.gitignore` keeps it out of git status). Sources are unpacked untouched in `src/`, built out of tree in `build/`, installed in `prefix/`.

Two packages have no release to pin: Tremor and Watt-32 are taken from the upstream `master`. The checksums in `sources.conf` are those of the snapshots this port was tested with, so a download made after upstream has changed stops with a checksum error. Keep a copy of the tarballs that worked (`build-dos-deps/dl`) with the release, or update the checksum after testing the newer one.

## Licences

`SCUMMVM.EXE` is ScummVM (GPL-3.0-or-later) with these libraries linked in. Four of them are under the GPL (libmad, libmpeg2, a52dec, faad2), which ScummVM's licence is compatible with; the others are BSD-style, zlib, MIT, IJG, FreeType's FTL, and the LGPL (FriBidi). `make-dist.sh` puts ScummVM's `COPYING.TXT` and a `LICENSES/` folder in `dist/`: the licence texts of every library, and an index (`INDEX.TXT`) with each library's licence and the URL its source comes from. Anyone who receives the program must be able to get the sources of the GPL libraries: the URLs and checksums in `sources.conf` are that offer, for the exact versions linked.

CWSDPMI.EXE is © Charles W Sandmann. You have the right to its source code and updates, available at https://sandmann.dotster.com/cwsdpmi/.

CWSDPMI's terms (`cwsdpmi.doc`, copied to `LICENSES/cwsdpmi/`) allow the binary to be redistributed without its source if it is not modified and users are told the above; `collect-licenses.sh` writes that notice into `LICENSES/INDEX.TXT`, and `make-dist.sh` refuses to ship `CWSDPMI.EXE` without it.

## Checking them

`tests/audiotest.c` links the three audio libraries and starts each one under DOS (an AC-3 decoder refusing junk, an AAC-LC decoder configured for 44.1 kHz stereo, a Musepack stream info); build line at the top of the file, prints `RESULT: OK`. It does not decode real audio.

`tests/nettest.c` starts Watt-32 under DOS and answers one web request (build line at the top of the file); try it in DOSBox-X with its NE2000 card and `Z:\SYSTEM\NE2000.COM 0x60 10 0x300` loaded.

`tests/libtest.c` uses every image/video library for real under DOS: a compression round trip, a PNG, a JPEG and a GIF decoded to the expected colours, FreeType and libmpeg2 started, libtheora refusing a junk header. Build and run it as the top of that file describes; it prints PASS or FAIL per library. `tests/mkimgs.py` makes its test pictures (needs macOS `sips`).

## Things that needed handling for DJGPP

- libogg's DJGPP header section lacks `ogg_uint16_t` (libtheora needs it); `build-libs.sh` patches the installed copy.
- libmpeg2: only the decoder is built. Its colour-conversion helper wants 32-byte alignment, which DJGPP's object format cannot give, and ScummVM does its own conversion. libmpeg2 picks its MMX code at run time by asking the processor, so a 486 never runs it.
- FreeType's own `freetype-config` asks the host's `pkg-config` and so finds the Mac's FreeType; `build-image-libs.sh` installs a replacement that knows only this prefix. For the same reason the ScummVM `reconfigure.sh` scripts point `PKG_CONFIG_LIBDIR` at `prefix/lib/pkgconfig`.
- libpng's and libjpeg's generated configuration headers (`pnglibconf.h`, `jconfig.h`) are not installed by their makefiles' usual targets; the script installs them.
- libpng's SSE code and libtheora's MMX assembly are switched off.
- libopenmpt is compiled directly from its "makefile" source package (no scripts run), the files its own Makefile lists, as GNU C++17 with exceptions, the way its `build/make/config-djgpp.mk` does, plus `-DNDEBUG`: its start-up self-check of the C library (`check_platform.hpp`) asserts that the program set `_crt0_startup_flags` to a non-moving `sbrk` without address wrap, which ScummVM does not and does not need (the program stopped on that assertion before `main`, with no message). zlib is the prefix's own; the bundled minimp3 and stb_vorbis decode the compressed samples of `.mo3` files. ScummVM's `configure` has no `--with-openmpt-prefix`: it finds the library through pkg-config, so the script installs `prefix/lib/pkgconfig/libopenmpt.pc` (`-lopenmpt -lz`).
- Watt-32 is built in a copy of its sources (the unpacked ones stay as they came): the "fast" packet-driver receiver (needs NASM) is left out, in favour of the ordinary DPMI callback; its private copy of zlib is left out, because its unrenamed `inflate_fast()`, `inflate_table()` and `inflateBack*()` replaced the real zlib's and corrupted everything it decompressed (themes failed their CRC check); `pcpkt.c` is compiled without function reordering, because it locks the receiver's memory as the range from its first function to its last (without that, the stack stops on an assertion); `sys/djgpp.err` and `syserr.c`, which its own build makes by running `util/errnos.c` as a DOS program, are made the same way under DOSBox-X.
