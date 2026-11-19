#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare RGB565 captures; allow hardware blend quantization only in alpha images."""
import struct
import sys
from pathlib import Path

a, b = (struct.unpack('<384000H', Path(p).read_bytes()) for p in sys.argv[1:3])
quantized = 0
for index, (sw, hw) in enumerate(zip(a, b)):
    if sw == hw:
        continue
    x, y = index % 800, index // 800
    alpha_region = 230 <= y < 294 and any(left <= x < left + 96 for left in (170, 320, 470))
    alpha_region |= (350 <= x < 550 and 330 <= y < 390) or (677 <= x < 756 and 120 <= y < 175)
    alpha_region |= (350 <= x < 374 or 626 <= x < 650) and (20 <= y < 44 or 76 <= y < 100)
    channels = ((sw >> 11, hw >> 11), ((sw >> 5) & 63, (hw >> 5) & 63), (sw & 31, hw & 31))
    if not alpha_region or any(abs(s - h) > 1 for s, h in channels):
        raise SystemExit(f'FAIL at ({x},{y}): software={sw:04x}, PPA={hw:04x}')
    quantized += 1
print(f'PASS: 384000 pixels compared; {quantized} alpha pixels differ by at most one RGB565 step/channel.')
