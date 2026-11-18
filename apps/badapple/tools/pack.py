#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Losslessly repack `perinkpheral video` output for the Bad Apple player.

BA01: magic, four little-endian u16 values (width, height, frame count,
frames per ten seconds), then count+1 absolute u16 frame offsets.
Each frame starts with a three-bit Exp-Golomb order and one initial bit.
Positive run lengths encode the vertically differenced pixels; unlike the
SDK video format, no initial zero-length run or length+1 bias is needed.
"""
import argparse
import struct
from pathlib import Path


def read_runs(data, pixels):
    position = 0
    total = 0
    runs = []

    def bit():
        nonlocal position
        if position >= len(data) * 8:
            raise ValueError("truncated frame")
        value = data[position // 8] >> (7 - position % 8) & 1
        position += 1
        return value

    while total < pixels:
        leading = 0
        while bit() == 0:
            leading += 1
            if leading > 16:
                raise ValueError("invalid run")
        value = 1
        for _ in range(leading):
            value = value * 2 + bit()
        length = value - 1
        if not length and runs:
            raise ValueError("zero-length interior run")
        runs.append(length)
        total += length
    if total != pixels:
        raise ValueError("frame exceeds dimensions")
    initial = 0 if runs[0] else 1
    return initial, runs if runs[0] else runs[1:]


def encode(initial, runs):
    def cost(order):
        return sum(2 * ((length + (1 << order) - 1).bit_length() - 1) - order + 1 for length in runs)

    order = min(range(8), key=cost)
    bits = f"{order:03b}{initial}"
    for length in runs:
        binary = f"{length + (1 << order) - 1:b}"
        bits += "0" * (len(binary) - 1 - order) + binary
    bits += "0" * (-len(bits) % 8)
    return bytes(int(bits[offset:offset + 8], 2) for offset in range(0, len(bits), 8))


def repack(data):
    width, height, count, rate = struct.unpack_from("<4H", data)
    if (width, height) != (200, 150) or not count or not rate:
        raise ValueError("expected 200x150 video with frames and a positive rate")
    offsets = struct.unpack_from(f"<{count + 1}I", data, 8)
    if offsets[0] != 8 + 4 * (count + 1) or offsets[-1] != len(data):
        raise ValueError("invalid frame table")
    frames = []
    for start, end in zip(offsets, offsets[1:]):
        if not 0 <= start < end <= len(data):
            raise ValueError("invalid frame offset")
        frames.append(encode(*read_runs(data[start:end], width * height)))
    offset = 12 + 2 * (count + 1)
    packed_offsets = [offset]
    for frame in frames:
        offset += len(frame)
        packed_offsets.append(offset)
    if offset > 65535 or max(map(len, frames)) > 2048:
        raise ValueError("video exceeds player limits")
    return b"BA01" + data[:8] + struct.pack(f"<{count + 1}H", *packed_offsets) + b"".join(frames)


if __name__ == "__main__":
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument("input", type=Path)
    arguments.add_argument("output", type=Path)
    args = arguments.parse_args()
    original = args.input.read_bytes()
    packed = repack(original)
    args.output.write_bytes(packed)
    print(f"{len(original)} -> {len(packed)} bytes; all frames and pixels retained")
