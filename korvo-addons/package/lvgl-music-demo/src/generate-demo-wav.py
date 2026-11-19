#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render the original demo melody on the build host, not the board CPU."""
import math
import struct
import sys
import wave

rate, seconds = 22050, 14
notes = (523.25, 659.25, 783.99, 659.25, 587.33, 698.46, 880.00, 698.46,
         523.25, 659.25, 783.99, 1046.5, 880.00, 783.99, 659.25, 523.25)
frames = rate * seconds
note_frames = frames // len(notes)
samples = bytearray()
for i in range(frames):
    position = i % note_frames
    edge = position / 350.0 if position < 350 else min((note_frames - position) / 650.0, 1.0)
    phase = i * notes[(i // note_frames) % len(notes)] * 2.0 * math.pi / rate
    value = (math.sin(phase) + .22 * math.sin(phase * 2) + .08 * math.sin(phase * 3)) * edge * .22
    sample = int(value * 32767.0)
    samples.extend(struct.pack('<hh', sample, sample))
with wave.open(sys.argv[1], 'wb') as output:
    output.setnchannels(2)
    output.setsampwidth(2)
    output.setframerate(rate)
    output.writeframes(samples)
