# MIDI and OPL3 on the DOS port

OPL2 / OPL3 output has been tried on real hardware (a Pentium 200 MMX with a Yamaha
OPL3, Sound Blaster Pro compatible, under FreeDOS with JEMMEX and the DPMI host that comes with FreeDOS), as well as in
DOSBox-X (an emulated Sound Blaster 16 / Sound Blaster 2). MIDI out through the MPU-401 has not
been tried on real hardware yet; it was exercised in DOSBox-X only (an emulated
MPU-401).

## MIDI out through an MPU-401 (`dos-midi.cpp`)

The `dosmidi` music plugin sends MIDI bytes to an MPU-401 interface in UART
mode: a Sound Blaster's MIDI port (with the game port MIDI cable), a Roland
MPU-401 or a compatible card. What sits at the other end decides the sound: a
Roland MT-32, a General MIDI module, or a Raspberry Pi running Munt or
FluidSynth that is plugged into the interface's MIDI out.

Two devices are offered, and only when an interface answers (it is reset and
put in UART mode when ScummVM looks for it):

| Device name            | Id in `music_driver`          | ScummVM sends |
|------------------------|-------------------------------|---------------|
| `Roland MT-32`         | `dosmidi_Roland MT-32`        | the MT-32's own setup and instrument messages |
| `General MIDI`         | `dosmidi_General MIDI`        | General MIDI data |

They are never chosen by `<default>`: an interface with nothing connected would
play silence. Select one in the launcher (Audio tab) or in `scummvm.ini`, in the
game's section or `[scummvm]`:

    music_driver=dosmidi_Roland MT-32
    native_mt32=true

or

    music_driver=dosmidi_General MIDI

`native_mt32=true` is the launcher's "True Roland MT-32" check box. Use it with a
real MT-32 (or Munt); leave it off for General MIDI.

### Port

The first of these that is set is used (all hexadecimal):

1. `SCUMMVM_DOS_MPU` in the environment
2. `mpu_port` in `scummvm.ini`
3. the `P` field of the `BLASTER` variable (`P330`)
4. 330

### Notes

- The sound card's FM chip (OPL) and its digital sound (speech and effects)
  carry on as usual; only the music moves to MIDI. The OPL emulation is not
  running, so MIDI music costs the CPU nothing beyond writing the bytes.
- Writing a byte waits until the interface has room, so messages go out at MIDI
  speed (31250 baud). While it waits, the driver keeps the sound card fed. After
  200 ms without room a byte is dropped and counted as a timeout.
- The MT-32 needs time between system exclusive messages. ScummVM's MT-32 code
  already adds it (the time on the wire plus 40 ms for the oldest models).
- `SCUMMVM_DOS_MIDILOG=1` logs the first messages sent and, every 512 bytes, the
  totals (bytes, note-ons, system exclusive messages, timeouts) to
  `scummvm.log`.

## OPL3 (`dos-opl.cpp`)

The FM driver (`opl_driver=dosopl`, the default when a chip answers) now serves
OPL3 requests as well as OPL2, when the card has an OPL3: a Sound Blaster Pro 2
or 16 and later. It tells the two apart by the status register after resetting
the timers (an OPL2 leaves bits 1 and 2 set, an OPL3 clears them).

- A game that asks for an OPL3 gets the second register set through ports
  0x38A / 0x38B, written to the real chip. Nothing is emulated.
- A game that asks for two OPL2 chips (a Sound Blaster Pro 1 setup) gets them
  as the left and right halves of the one OPL3, using ScummVM's usual
  dual-OPL2-on-OPL3 handling.
- On an OPL2-only card an OPL3 or dual-OPL2 request is declined, as before, so
  engines that can do without fall back to a single OPL2.
- OPL3 mode is switched off again when ScummVM exits.

`SCUMMVM_DOS_OPL3TEST=1` asks for an OPL3 once at start-up, writes to both
register sets and logs `DOSOPL3TEST: ...` to `scummvm.log`.
