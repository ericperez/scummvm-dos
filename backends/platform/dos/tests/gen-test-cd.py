#!/usr/bin/env python3
"""Write a music CD image of three tracks and a track file for the DOS CD audio tests.

  AUDIO.BIN  raw CD audio (2352 bytes a frame, 75 frames a second), three tracks of
             4, 6 and 3 seconds: 300, 450 and 225 frames, a different tone on each
  AUDIO.CUE  its cue sheet (DOSBox-X mounts it as a CD: imgmount d AUDIO.CUE -t iso)
  TRACK2.WAV the second track as a file, for the test without a drive (a game that
             ships its CD tracks as files)

Usage: gen-test-cd.py <output folder> [image|files]
  image  only the CD image (the test of the drive: a track file would be played in its place)
  files  only the track file
  (no second argument: both)
"""
import math
import os
import struct
import sys

FRAME_BYTES = 2352            # 588 stereo samples of 16 bits
SAMPLES_PER_FRAME = 588
TRACKS = [(300, 440.0), (450, 660.0), (225, 880.0)]   # frames, tone in Hz


def tone(frames, hertz, first_sample=0):
    out = bytearray()
    for i in range(frames * SAMPLES_PER_FRAME):
        v = int(12000 * math.sin(2 * math.pi * hertz * (first_sample + i) / 44100.0))
        out += struct.pack("<hh", v, v)
    return bytes(out)


def msf(frames):
    return "%02d:%02d:%02d" % (frames // (75 * 60), (frames // 75) % 60, frames % 75)


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    what = sys.argv[2] if len(sys.argv) > 2 else "both"
    os.makedirs(folder, exist_ok=True)

    if what in ("both", "image"):
        with open(os.path.join(folder, "AUDIO.BIN"), "wb") as f:
            for frames, hertz in TRACKS:
                f.write(tone(frames, hertz))

        cue = ['FILE "AUDIO.BIN" BINARY']
        start = 0
        for number, (frames, _) in enumerate(TRACKS, 1):
            cue.append("  TRACK %02d AUDIO" % number)
            cue.append("    INDEX 01 %s" % msf(start))
            start += frames
        with open(os.path.join(folder, "AUDIO.CUE"), "w", newline="\r\n") as f:
            f.write("\n".join(cue) + "\n")

    if what in ("both", "files"):
        # Track 2 as a 16 bit stereo 44.1 kHz WAV, 2 seconds of its tone
        pcm = tone(150, 660.0)
        with open(os.path.join(folder, "TRACK2.WAV"), "wb") as f:
            f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE")
            f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, 44100, 44100 * 4, 4, 16))
            f.write(b"data" + struct.pack("<I", len(pcm)) + pcm)

    print("%d tracks, %d frames: %s" % (len(TRACKS), sum(t[0] for t in TRACKS),
                                        ",".join(str(t[0]) for t in TRACKS)))


if __name__ == "__main__":
    main()
