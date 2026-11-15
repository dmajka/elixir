#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Elixir: The Firmware Hub's card picture (docs/card.png, fmhub.json's "image"). 1.4.1: two of the FM-1's screens side
by side (ui_test --card: a beat with generators on it, the GENERATOR page on GRIDS; WANDER on THE SPIRE), each doubled
pixel for pixel, under the splash's potion and word, over "drum synth & sequencer" in Inter Tight. 1008 x 1008 PNG
(the hub: square, 240 to 1024 px, 300 KB at most). Before 1.4.1: the splash's potion and word alone, 480 x 480.

    build/ui_test build/shots && build/ui_test build/card --card && tools/make_card.py   (Pillow: PYTHON=.venv/bin/python)"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import site_shots                                     # noqa: E402  (its PPM reader)

S = 1008                                              # the card, square
SCR = 480                                             # a screen, doubled
GAP = 20                                              # between the two screens


def ppm(path):
    w, h, rgb = site_shots.read_ppm(path)
    return Image.frombytes("RGB", (w, h), rgb)


def main(out=ROOT / "docs/card.png"):
    splash = ppm(sorted((ROOT / "build/shots").glob("[0-9]*-splash.ppm"))[0])
    potion = splash.crop((65, 10, 175, 100)).resize((220, 180), Image.NEAREST)   # (the splash's ink boxes, doubled)
    word = splash.crop((30, 114, 213, 152)).resize((366, 76), Image.NEAREST)
    screens = [ppm(ROOT / "build/card" / f"card-{n}.ppm").resize((SCR, SCR), Image.NEAREST) for n in ("seq", "wander")]
    font = ImageFont.truetype(str(ROOT / "assets/fonts/InterTight[wght].ttf"), 46)
    font.set_variation_by_axes([650])
    text = "drum synth & sequencer"
    card = Image.new("RGB", (S, S), (0, 0, 0))
    d = ImageDraw.Draw(card)
    y = (S - (180 + 40 + SCR + 36 + 46)) // 2
    x = (S - (220 + 28 + 366)) // 2                  # the potion beside the word
    card.paste(potion, (x, y))
    card.paste(word, (x + 220 + 28, y + (180 - 76) // 2 + 10))
    y += 180 + 40
    x = (S - (2 * SCR + GAP)) // 2
    for k, im in enumerate(screens):                 # each in a dark bezel
        sx = x + k * (SCR + GAP)
        d.rounded_rectangle((sx - 6, y - 6, sx + SCR + 5, y + SCR + 5), 14, fill=(28, 26, 23))
        card.paste(im, (sx, y))
    y += SCR + 36
    d.text(((S - d.textlength(text, font=font)) / 2, y - 8), text, font=font, fill=(242, 237, 227))
    card.save(out, optimize=True)
    print(f"make_card: {out} ({Path(out).stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main(*sys.argv[1:2])
