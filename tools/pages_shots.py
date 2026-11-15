#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Elixir 1.4.1: the site's Pages (web/pages/*.png, pages.json) from ui_test's tour (build/tour/pages: every page of
every mode as SELECT walks them, and pages.tsv: section, group, page, pages, title, file).

    build/ui_test build/tour --pages && tools/pages_shots.py

The screens are the firmware's own. web/make_site.py builds pages/ from what this writes. Plain Python, no Pillow."""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from site_shots import png, read_ppm  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def main(src=ROOT / "build/tour/pages", out=ROOT / "web/pages"):
    src, out = Path(src), Path(out)
    rows = [line.rstrip("\n").split("\t") for line in (src / "pages.tsv").read_text(encoding="utf-8").splitlines()]
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.png"):
        old.unlink()
    pages = []
    for sec, grp, n, of, title, ppm in rows:
        name = ppm.replace(".ppm", ".png")
        (out / name).write_bytes(png(*read_ppm(src / ppm)))
        pages.append({"section": sec, "group": grp, "n": int(n), "of": int(of), "title": title, "file": name})
    (out / "pages.json").write_text(json.dumps(pages, indent=1) + "\n", encoding="utf-8")
    print(f"pages_shots: {len(pages)} screens -> {out}")


if __name__ == "__main__":
    main(*sys.argv[1:3])
