#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Elixir 0.14: the front page's screenshots (web/shots/*.png) from ui_test's (build/shots/*.ppm).

    build/ui_test build/shots && tools/site_shots.py

The screens are the firmware's own, drawn by tests/ui_test.c. Plain Python (zlib), no Pillow."""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# shown on the front page, in this order: ui_test's name -> the site's
SHOTS = {"03-playing": "pattern", "58-grids-map": "grids", "67-perform2": "perform",
         "90-global-lfo": "global-lfo", "63-links-duck-hats": "links", "12-mixer": "mixer", "73-keys": "keys"}


def read_ppm(path):
    data = path.read_bytes()
    head = data.split(maxsplit=4)                     # P6 width height maxval pixels
    if head[0] != b"P6" or head[3] != b"255":
        raise SystemExit(f"{path}: not an 8-bit P6 PPM")
    w, h = int(head[1]), int(head[2])
    return w, h, data[len(data) - w * h * 3:]


def png(w, h, rgb):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    rows = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def main(src=ROOT / "build/shots", out=ROOT / "web/shots"):
    src, out = Path(src), Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for name, to in SHOTS.items():
        w, h, rgb = read_ppm(src / f"{name}.ppm")
        (out / f"{to}.png").write_bytes(png(w, h, rgb))
    print(f"site_shots: {len(SHOTS)} screens -> {out}")


if __name__ == "__main__":
    main(*sys.argv[1:3])
