#!/usr/bin/env python3
"""Write a small tracker module for the DOS tracker music tests: one looped square-wave sample and one pattern of
64 rows playing it on four channels, a note every 16 rows. The format is the file's extension: .mod (ProTracker
M.K.), .s3m (Scream Tracker 3), .xm (FastTracker 2) or .it (Impulse Tracker).

Usage: gen-test-mod.py <output file>
"""
import os
import struct
import sys

SAMPLE_LEN = 64                 # bytes: half +96, half -96 (signed 8-bit)
PERIODS = [428, 381, 339, 320]  # MOD: C-2, D-2, E-2, F-2
NOTES = [0, 2, 4, 5]            # the same notes as semitones above C


def name(text, size):
    return text.encode('ascii').ljust(size, b'\0')


def square(signed=True):
    half = SAMPLE_LEN // 2
    if signed:
        return bytes([0x60] * half + [0xA0] * half)
    return bytes([0xE0] * half + [0x20] * half)


def notes():
    """(row, channel, semitone) of every note in the pattern"""
    for row in range(0, 64, 16):
        for channel in range(4):
            yield row, channel, NOTES[(row // 16 + channel) % 4]


def pad16(out):
    out += bytes(-len(out) % 16)


def write_mod():
    out = bytearray(name('scummvm dos test', 20))
    # Sample 1: length, finetune, volume, loop start, loop length (in words); 2 to 31 empty
    words = SAMPLE_LEN // 2
    out += name('square', 22) + struct.pack('>HBBHH', words, 0, 64, 0, words)
    for _ in range(30):
        out += name('', 22) + struct.pack('>HBBHH', 0, 0, 0, 0, 1)
    out += bytes([1, 127]) + bytes(128) + b'M.K.'

    cells = {(row, channel): note for row, channel, note in notes()}
    for row in range(64):
        for channel in range(4):
            if (row, channel) in cells:
                period = PERIODS[NOTES.index(cells[row, channel])]
                out += bytes([period >> 8, period & 0xFF, 0x10, 0])
            else:
                out += bytes(4)
    return out + square()


def write_s3m():
    # Header, orders (one pattern, then the end), the instrument's and the pattern's paragraph pointers
    out = bytearray(name('scummvm dos test', 28) + bytes([0x1A, 16, 0, 0]))
    out += struct.pack('<6H', 2, 1, 1, 0, 0x1320, 2) + b'SCRM'      # unsigned samples (2)
    out += bytes([64, 6, 125, 0xB0, 0, 0]) + bytes(8) + struct.pack('<H', 0)
    out += bytes([0, 8, 1, 9] + [255] * 28)                       # channels: left, right, left, right
    out += bytes([0, 255])
    pointers = len(out)
    out += bytes(4)
    pad16(out)

    instrument = len(out)
    out += bytes(0x50)
    pad16(out)
    pattern = len(out)
    rows = bytearray()
    cells = {}
    for row, channel, note in notes():
        cells.setdefault(row, []).append((channel, note))
    for row in range(64):
        for channel, note in cells.get(row, []):
            rows += bytes([channel | 0x20, 0x40 | note, 1])        # note (octave 4) and instrument
        rows += b'\0'
    out += struct.pack('<H', len(rows) + 2) + rows
    pad16(out)
    data = len(out)
    out += square(signed=False)

    struct.pack_into('<HH', out, pointers, instrument >> 4, pattern >> 4)
    struct.pack_into('<B12sBH3I', out, instrument, 1, name('square', 12), data >> 20, (data >> 4) & 0xFFFF,
                     SAMPLE_LEN, 0, SAMPLE_LEN)
    struct.pack_into('<BBBBI', out, instrument + 0x1C, 64, 0, 0, 1, 8363)  # volume, packing, looped, C4 speed
    struct.pack_into('<28s4s', out, instrument + 0x30, name('square', 28), b'SCRS')
    return out


def write_xm():
    out = bytearray(b'Extended Module: ' + name('scummvm dos test', 20) + b'\x1A' + name('scummvm', 20))
    out += struct.pack('<HI8H', 0x0104, 276, 1, 0, 4, 1, 1, 1, 6, 125) + bytes(256)

    # The pattern, packed: 0x80 alone is an empty cell, 0x83 a note and an instrument
    cells = {(row, channel): note for row, channel, note in notes()}
    rows = bytearray()
    for row in range(64):
        for channel in range(4):
            if (row, channel) in cells:
                rows += bytes([0x83, 49 + cells[row, channel], 1])  # C-4 is 49
            else:
                rows += b'\x80'
    out += struct.pack('<IBHH', 9, 0, 64, len(rows)) + rows

    # The instrument (263 bytes of header, one sample) and its sample (40 bytes), then the sample's deltas
    header = bytearray(struct.pack('<I22sBHI', 263, name('square', 22), 0, 1, 40))
    header += bytes(96 + 48 + 48 + 14) + struct.pack('<H', 0)
    header += bytes(263 - len(header))
    out += header
    out += struct.pack('<3IBbBBbB22s', SAMPLE_LEN, 0, SAMPLE_LEN, 64, 0, 1, 128, 0, 0, name('square', 22))
    previous = 0
    for byte in square():
        value = byte - 256 if byte > 127 else byte
        out.append((value - previous) & 0xFF)
        previous = value
    return out


def write_it():
    out = bytearray(b'IMPM' + name('scummvm dos test', 26))
    out += struct.pack('<8H', 0x1004, 2, 0, 1, 1, 0x0214, 0x0214, 0x09)  # stereo, linear slides, samples only
    out += struct.pack('<H6BHII', 0, 128, 48, 6, 125, 128, 0, 0, 0, 0)
    out += bytes([0, 64, 0, 64] + [160] * 60)                        # panning (160: channel off)
    out += bytes([64] * 64)
    out += bytes([0, 255])
    pointers = len(out)
    out += bytes(8)

    sample = len(out)
    out += bytes(80)
    pattern = len(out)
    cells = {}
    for row, channel, note in notes():
        cells.setdefault(row, []).append((channel, note))
    rows = bytearray()
    for row in range(64):
        for channel, note in cells.get(row, []):
            rows += bytes([(channel + 1) | 0x80, 3, 60 + note, 1])    # C-5 is 60, sample 1
        rows += b'\0'
    out += struct.pack('<HH4x', len(rows), 64) + rows
    data = len(out)
    out += square()

    struct.pack_into('<II', out, pointers, sample, pattern)
    # Present and looped (0x11), signed (1), C5 speed, then the sample's offset
    struct.pack_into('<4s12sBBBB26sBB7I', out, sample, b'IMPS', name('square', 12), 0, 64, 0x11, 64,
                     name('square', 26), 1, 0, SAMPLE_LEN, 0, SAMPLE_LEN, 8363, 0, 0, data)
    return out


WRITERS = {'.mod': write_mod, '.s3m': write_s3m, '.xm': write_xm, '.it': write_it}


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    ext = os.path.splitext(sys.argv[1])[1].lower()
    if ext not in WRITERS:
        sys.exit('unknown module format: ' + ext)
    with open(sys.argv[1], 'wb') as f:
        f.write(WRITERS[ext]())


if __name__ == '__main__':
    main()
