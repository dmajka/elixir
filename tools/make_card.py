#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Elixir 1.0: The Firmware Hub's card picture (docs/card.png, fmhub.json's "image"): the splash's potion and word
(ui_test's 00-splash, so the firmware's own pixels) doubled, over "drum synth & sequencer" in Inter Tight. 480 x 480 PNG.

    build/ui_test build/shots && tools/make_card.py        (needs Pillow: PYTHON=.venv/bin/python)"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import site_shots                                     # noqa: E402  (its PPM reader)


def main(out=ROOT / "docs/card.png"):
    w, h, rgb = site_shots.read_ppm(sorted((ROOT / "build/shots").glob("[0-9]*-splash.ppm"))[0])
    art = Image.frombytes("RGB", (w, h), rgb).crop((0, 10, 240, 152)).resize((480, 284), Image.NEAREST)
    font = ImageFont.truetype(str(ROOT / "assets/fonts/InterTight[wght].ttf"), 38)
    font.set_variation_by_axes([650])
    card, text = Image.new("RGB", (480, 480), (0, 0, 0)), "drum synth & sequencer"
    y0 = (480 - (art.height + 28 + 38)) // 2
    card.paste(art, (0, y0))
    d = ImageDraw.Draw(card)
    d.text(((480 - d.textlength(text, font=font)) / 2, y0 + art.height + 28), text, font=font, fill=(242, 237, 227))
    card.save(out, optimize=True)
    print(f"make_card: {out}")


if __name__ == "__main__":
    main(*sys.argv[1:2])
