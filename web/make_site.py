#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Elixir's site (GitHub Pages or any static host), after Felucca's web/make_site.py (Copyright (C) 2026 Leo Kuroshita
# (@kurogedelic), Hügelton Instruments):
#
#   index.html                  the front page (web/index.html; 0.14), site.css, favicon.svg, shots/ (its screens)
#   install/                    the install page: web/install.html with fm1pkg.js, fm1ota.js, install.js and the
#                               package's details inlined (the front page until 0.13)
#   try/                        the emulator (build/emu: run web/emu/build.sh first)
#   mobile/                     the emulator laid out for phones (web/emu/mobile.html, on try/'s files; 0.16):
#                               not linked from anywhere and noindex while it's tried out
#   library/                    backup, restore and LXR-02 kit import over Web MIDI (web/library.html, library.js)
#   manual/index.html           MANUAL.md as a page, with docs/img (its signal-flow diagrams: tools/flow_svg.py) in
#                               manual/img, and elixir-manual-VER.pdf, the page printed (headless Chrome)
#   cheatsheet.pdf              four pages of keys and knobs (web/cheatsheet.pdf: tools/cheatsheet.sh); fmhub.json
#                               links to it
#   firmware/                   elixir-VER.fwsc, elixir-VER-src.tar.gz (the source, as the GPL asks), MANUAL.md,
#                               LICENSE, NOTICE.md, LICENSES/, and a page listing them
#
#   web/make_site.py [PACKAGE.fwsc] [OUT_DIR]      (default: build/elixir.fwsc build/site)
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = "https://github.com/dmajka/elixir"   # the source repository's URL, once public: the pages link it
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
SRC = ["build.sh", "LICENSE", "LICENSES", "NOTICE.md", "README.md", "MANUAL.md",
       "docs", "firmware", "tools", "tests", "web", "assets"]


# the header on every page: depth 0 at the root, 1 a folder down; here: the page's own link
NAV = [("try/", "Try it"), ("install/", "Install"), ("manual/", "Manual"), ("cheatsheet.pdf", "Cheat sheet"),
       ("library/", "Library")]


def nav(depth, here=None):
    up = "../" * depth
    links = "".join(f'<a href="{up}{href}"' + (' aria-current="page"' if href == here else "") + f">{text}</a>"
                    for href, text in NAV)
    if REPO:
        links += f'<a href="{REPO}">Source</a>'
    return (f'<header class="site"><div class="wrap"><a class="brand" href="{up or "./"}">{potion_svg(small=True)}'
            f"Elixir</a><nav>{links}</nav></div></header>")


# the header on a page without site.css (try/: the emulator has its own styles, and site.css's would restyle its
# buttons): the same header, on a dark bar, its rules scoped to it
NAV_CSS = """<style>
header.site { background: #131210; border-bottom: 1px solid #2d2a25; font: 15px/1.4 -apple-system, BlinkMacSystemFont,
  "Segoe UI", "Helvetica Neue", Arial, sans-serif; -webkit-font-smoothing: antialiased; }
header.site .wrap { max-width: 1040px; margin: 0 auto; padding: 0 16px; display: flex; align-items: center; gap: 24px;
  min-height: 56px; flex-wrap: wrap; box-sizing: border-box; }
header.site .brand { display: flex; align-items: center; gap: 10px; color: #f2ede3; text-decoration: none;
  font-weight: 650; font-size: 19px; letter-spacing: .02em; }
header.site .brand svg { width: 26px; height: auto; display: block; shape-rendering: crispEdges; }
header.site nav { display: flex; gap: 20px; flex-wrap: wrap; margin-left: auto; }
header.site nav a { color: #a29b8e; text-decoration: none; padding: 4px 0; }
header.site nav a:hover, header.site nav a[aria-current] { color: #f2ede3; }
header.site nav a[aria-current] { box-shadow: inset 0 -2px #f0a43a; }
header.site .px-c { fill: #96603a; } header.site .px-o { fill: #f2ede3; } header.site .px-g { fill: rgba(242,237,227,.08); }
header.site .px-h { fill: #fff; } header.site .px-l { fill: #f0a43a; } header.site .px-L { fill: #f8d394; }
header.site .px-d { fill: #a8640f; }
@media (max-width: 560px) { header.site .wrap { gap: 4px 16px; padding-top: 10px; padding-bottom: 10px; }
  header.site nav { margin-left: 0; gap: 14px; } }
@media (max-width: 420px) { header.site nav { gap: 2px 12px; font-size: 14px; } }
</style>"""


# the pixel art: the splash's potion and word (firmware/src/ui_draw.c POTION, WORD, WORD_W), as svg, a class a colour
def pixel_art():
    c = (ROOT / "firmware/src/ui_draw.c").read_text()
    potion = re.findall(r'"([.a-zA-Z]{22})"', c[c.index("POTION[18][23]"):c.index("WORD[6][7]")])
    rows = re.findall(r"\{(0x[0-9A-F]{2}(?:, 0x[0-9A-F]{2}){6})\}", c[c.index("WORD[6][7]"):])
    word = [[int(x, 16) for x in r.split(", ")] for r in rows]
    widths = [int(x) for x in re.search(r"WORD_W\[6\] = \{([^}]*)\}", c)[1].split(",")]
    if len(potion) != 18 or len(word) != 6:
        raise SystemExit("firmware/src/ui_draw.c: POTION or WORD not found")
    return potion, word, widths


def rects(cells):                                    # {class: [(x, y)]} -> svg paths, a run of cells a rectangle
    out = []
    for cls, pts in cells.items():
        d, pts = [], sorted(set(pts), key=lambda p: (p[1], p[0]))
        i = 0
        while i < len(pts):
            x, y = pts[i]
            n = 1
            while i + n < len(pts) and pts[i + n] == (x + n, y):
                n += 1
            d.append(f"M{x} {y}h{n}v1h-{n}z")
            i += n
        out.append(f'<path class="px-{cls}" d="{"".join(d)}"/>')
    return "".join(out)


def potion_svg(small=False):
    potion = pixel_art()[0]
    cells = {}
    for y, row in enumerate(potion):
        for x, ch in enumerate(row):
            if ch != "." and not (small and ch == "s"):
                cells.setdefault(ch, []).append((x, y))
    box = "4 0 14 18" if small else "0 0 22 18"   # (small: the flask alone)
    return f'<svg class="pixel" viewBox="{box}" aria-hidden="true">{rects(cells)}</svg>'


def word_svg():
    _, word, widths = pixel_art()
    cells, x0 = {"ws": [], "w": []}, 0
    for glyph, w in zip(word, widths):
        for y, bits in enumerate(glyph):
            for k in range(w):
                if bits >> (w - 1 - k) & 1:
                    cells["w"].append((x0 + k, y))
        x0 += w + 2
    width = x0 - 2
    shadow = rects({"ws": cells["w"]}).replace("<path", '<path transform="translate(0.6 0.6)"')
    return (f'<svg class="pixel" viewBox="0 0 {width + 1} 8" role="img" aria-label="Elixir">'
            f"{shadow}{rects({'w': cells['w']})}</svg>")


def favicon():
    css = ".px-c{fill:#96603a}.px-o{fill:#1d1b18}.px-g{fill:#fff}.px-h{fill:#fff}.px-l{fill:#f0a43a}" \
          ".px-L{fill:#f8d394}.px-d{fill:#a8640f}"
    return potion_svg(small=True).replace('class="pixel" ', 'xmlns="http://www.w3.org/2000/svg" ', 1) \
        .replace(">", f"><style>{css}</style>", 1)


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def version():
    m = re.search(r'#define ELIXIR_VERSION "([^"]+)"', (ROOT / "firmware/src/elixir.c").read_text())
    return m[1]


# the manual: the Markdown MANUAL.md uses (headings, paragraphs, lists, tables, fenced code, rules, emphasis, code,
# links), with GitHub's heading anchors so its links work. A line <!-- grid --> (hidden on GitHub) makes the table
# after it a grid: every cell boxed and centred, the last column shaded (the remote map's grids, section 15)
def slug(text):
    return re.sub(r"[^\w\- ]", "", text.lower()).replace(" ", "-")


# the link mark after each heading (the manual: a click copies the section's address)
LINK_SVG = ('<svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path fill="none" stroke="currentColor" '
            'stroke-width="1.6" stroke-linecap="round" d="M6.8 9.2a3 3 0 0 0 4.2 0l2.2-2.2a3 3 0 0 0-4.2-4.2l-1 1M9.2 '
            '6.8a3 3 0 0 0-4.2 0L2.8 9a3 3 0 0 0 4.2 4.2l1-1"/></svg>')


def inline(s):
    out, parts = [], re.split(r"(`[^`]*`)", s)
    for p in parts:
        if p.startswith("`") and p.endswith("`") and len(p) > 1:
            out.append("<code>" + html.escape(p[1:-1]) + "</code>")
            continue
        p = html.escape(p, quote=False)
        p = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", lambda m: f'<a href="{m[2]}"' +
                   (' target="_blank" rel="noopener"' if m[2].startswith("http") else "") + f">{m[1]}</a>", p)
        p = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", p)
        p = re.sub(r"(?<![\w*])\*([^*]+?)\*(?![\w*])", r"<em>\1</em>", p)
        out.append(p)
    return "".join(out)


def cells(row):
    return [c.strip() for c in row.strip().strip("|").split("|")]


def markdown(md):
    """md -> (title, body, heads): heads the (level, id, text) of its h2s and h3s, for the side bar"""
    lines, out, i, title, heads, grid = md.split("\n"), [], 0, "Elixir manual", [], False
    while i < len(lines):
        ln = lines[i]
        if ln.strip() == "<!-- grid -->":
            grid = True
            i += 1
        elif ln.strip() == "<!-- panel -->":           # (the FM-1 at a glance: panel_svg, after the parse)
            out.append("<!--PANEL-->")
            i += 1
        elif ln.startswith("```"):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("```"):
                j += 1
            out.append("<pre><code>" + html.escape("\n".join(lines[i + 1:j])) + "</code></pre>")
            i = j + 1
        elif m := re.match(r"(#{1,6}) (.*)", ln):
            n, text = len(m[1]), m[2].strip()
            if n == 1:
                title = text
            elif n <= 3:
                heads.append((n, slug(text), text))
            mark = f' <a class="hl" href="#{slug(text)}" aria-label="Copy a link to this section">{LINK_SVG}</a>' if n > 1 else ""
            out.append(f'<h{n} id="{slug(text)}">{inline(text)}{mark}</h{n}>')
            i += 1
        elif m := re.fullmatch(r"!\[([^\]]*)\]\(([^)\s]+)\)", ln.strip()):   # an image alone on its line: a figure
            src = re.sub(r"^docs/img/", "img/", m[2])  # (docs/img: copied to manual/img)
            out.append(f'<figure class="flow"><img src="{html.escape(src)}" alt="{html.escape(m[1])}" loading="lazy">'
                       f"<figcaption>{inline(m[1])}</figcaption></figure>")
            i += 1
        elif ln.strip() == "---":
            out.append("<hr>")
            i += 1
        elif ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append(lines[i])
                i += 1
            head, body = cells(rows[0]), [cells(r) for r in rows[2:]]
            out.append(f'<div class="table{" grid" if grid else ""}"><table><thead><tr>' + "".join(f"<th>{inline(c)}</th>" for c in head) +
                       "</tr></thead><tbody>" + "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) +
                                                       "</tr>" for r in body) + "</tbody></table></div>")
            grid = False
        elif re.match(r"(\d+\.|[-*]) ", ln):
            tag = "ol" if ln[0].isdigit() else "ul"
            items = []
            while i < len(lines) and re.match(r"(\d+\.|[-*]) ", lines[i]):
                items.append(re.sub(r"^(\d+\.|[-*]) ", "", lines[i]))
                i += 1
                while i < len(lines) and lines[i].startswith("   ") and lines[i].strip():
                    items[-1] += " " + lines[i].strip()
                    i += 1
            out.append(f"<{tag}>" + "".join(f"<li>{inline(t)}</li>" for t in items) + f"</{tag}>")
        elif not ln.strip():
            i += 1
        else:
            para = []
            while i < len(lines) and lines[i].strip() and not re.match(r"(#|```|\||---$|\d+\. |[-*] |!\[)", lines[i]):
                para.append(lines[i].strip())
                i += 1
            out.append("<p>" + inline(" ".join(para)) + "</p>")
    return title, "\n".join(out), heads


# the manual's chapters (the site's only: MANUAL.md stays numbered sections). Each groups sections by number
# ("A" the appendix), and gets an opening page (chapter_head) before its first section and a heading in the side bar
CHAPTERS = [
    ("I", "The Threshold", "Installing, a first beat, and the panel", [1, 2, 3]),
    ("II", "The Book of Rhythm", "Tracks, patterns, steps, recording and songs", [4, 5, 6, 7, 8]),
    ("III", "The Book of Sound", "Voices, the mixer and effects, kits and morph", [9, 10, 11]),
    ("IV", "The Book of Conjuring", "Generators, performance, spells and wandering", [12, 13]),
    ("V", "The Keeping", "Saving, MIDI and settings", [14, 15, 16]),
    ("VI", "The Appendices", "Tables, differences, troubleshooting and credits", [17, 18, 19, 20, "A"]),
]


def sec_no(text):                                    # "12. Generating tracks" -> 12, "Appendix: .." -> "A"
    m = re.match(r"(\d+)\.", text)
    return int(m[1]) if m else "A" if text.lower().startswith("appendix") else None


def sections(heads):                                 # {section number: its heading's id}
    return {sec_no(t): hid for n, hid, t in heads if n == 2 and sec_no(t) is not None}


def chapter_of(no):
    return next((c for c in CHAPTERS if no in c[3]), None)


def chapter_head(c):
    num, name, line, _ = c
    return (f'<header class="chapter" id="chapter-{num.lower()}"><p class="cnum">Chapter {num}</p>'
            f'<p class="ctitle" role="heading" aria-level="2">{name}</p><p class="cline">{line}</p>'
            '<p class="flourish" aria-hidden="true"><span>&#x25C6;</span></p></header>')


def chapters(body, heads):
    """each chapter's opening page put before its first section"""
    sec = sections(heads)
    for c in CHAPTERS:
        first = next((sec[n] for n in c[3] if n in sec), None)
        if first:
            mark = f'<h2 id="{first}">'
            if body.count(mark) != 1:
                raise SystemExit(f"MANUAL.md: section {first} not found once")
            body = body.replace(mark, chapter_head(c) + "\n" + mark)
    return body


def toc(heads):
    """the side bar: the chapters, their sections, each with its subsections (shown for the section being read)"""
    out, sub, li, chap = [], False, False, None
    for n, hid, text in heads:
        if n == 2:
            if sub:
                out.append("</ul>")
                sub = False
            if li:
                out.append("</li>")
            c = chapter_of(sec_no(text))
            if c is not chap or not out:
                if out:
                    out.append("</ul>")
                chap = c
                if c:
                    out.append(f'<p class="chap"><a href="#chapter-{c[0].lower()}"><span>{c[0]}</span> {c[1]}</a></p>')
                out.append("<ul>")
            out.append(f'<li><a href="#{hid}">{inline(text)}</a>')
            li = True
        else:
            if not sub:
                out.append("<ul>")
                sub = True
            out.append(f'<li><a href="#{hid}">{inline(text)}</a></li>')
    out.append(("</ul>" if sub else "") + ("</li>" if li else "") + "</ul>")
    return "".join(out)


# the FM-1 at a glance (MANUAL.md's <!-- panel --> line): the panel as the emulator draws it (web/emu/index.html
# LAYOUTS.wide), each control linking to the section that explains it, and the same as a table under it for phones
PANEL_SEC = {                                        # control: (sections, what it does)
    "SCREEN": ([3], "The grid, or the page and its four knobs"),
    "MASTER": ([10], "Output volume"),
    "SELECT": ([3], "The page; in lists, the row"),
    "PRESETS": ([11], "The kit browser; on a sound page, sounds"),
    "ALGORITHM": ([4], "Selects the track"),
    "KNOB 1": ([9], "The four parameters on screen"),
    "OCT-": ([3], "SHIFT (hold): what each key does on screen"),
    "OCT+": ([3], "COPY (hold), OK in dialogs; with SHIFT: CLEAR"),
    "FX": ([10], "FX bus, reverb, dirt, compressor"),
    "SCL": ([9], "Filter, shape, insert FX"),
    "ENV": ([9], "Amp envelope, gate, mod envelope"),
    "LFO": ([9], "LFOs, random per hit, cross mod"),
    "EDIT": ([9], "Model, oscillator, FM, transient; SHIFT: KEYS"),
    "GLO": ([10, 16], "Mixer, then CONFIG"),
    "HOME": ([5, 8], "Grid, patterns, song"),
    "SAVE": ([14], "Save and load"),
    "ARP": ([13], "PERFORM, SPELLS, WANDER, GLOBAL MOD"),
    "SEQ": ([6, 12], "Track pages, generators, step edit"),
    "PLAY": ([8], "Start and stop the pattern or song"),
    "REC": ([7], "Record; SHIFT: record options"),
    "WHITE": ([5], "White keys 1–16: the steps (patterns, rolls)"),
    "TRACKS": ([4], "Black keys 1–7: tracks D1 D2 D3 SN CY CH OH"),
    "BARS": ([8], "Black keys 8–11: bars 1–4, banks"),
}


def panel_svg(heads):
    sec = sections(heads)
    title = {n: t for n2, hid, t in heads if n2 == 2 for n in [sec_no(t)]}

    def ref(name):
        nums = PANEL_SEC[name][0]
        return f"#{sec[nums[0]]}", "·".join(str(n) for n in nums)

    def link(name, inner, label):
        href, _ = ref(name)
        return (f'<a href="{href}" class="ctl"><title>{html.escape(label)}: {html.escape(PANEL_SEC[name][1])} '
                f'(section {ref(name)[1]})</title>{inner}</a>')

    def badge(x, y, name):
        n = ref(name)[1]
        w = 12 + 8 * len(n)
        return (f'<rect class="bdg" x="{x - w / 2:.1f}" y="{y - 10}" width="{w}" height="20" rx="10"/>'
                f'<text class="bdgt" x="{x}" y="{y + 4.5}">{n}</text>')

    s = ['<svg class="panel" viewBox="0 0 1000 624" role="img" aria-label="The M-VAVE FM-1\'s panel: each control '
         'with the number of the section that explains it">',
         '<rect class="body" x="4" y="4" width="992" height="616" rx="40"/>',
         '<rect class="tray" x="50" y="250" width="220" height="50" rx="14"/>',
         '<rect class="tray" x="568" y="160" width="400" height="140" rx="14"/>']
    s.append(link("SCREEN", '<rect class="scr" x="292" y="44" width="256" height="256" rx="18"/>'
                  '<rect class="glass" x="300" y="52" width="240" height="240" rx="6"/>'
                  '<image href="../shots/pattern.png" x="300" y="52" width="240" height="240" '
                  'preserveAspectRatio="xMidYMid slice"/>' + badge(526, 66, "SCREEN"), "Screen"))
    knobs = [("MASTER", 105, 100), ("SELECT", 215, 100), ("PRESETS", 105, 205), ("ALGORITHM", 215, 205)]
    for name, x, y in knobs:
        s.append(link(name, f'<circle class="tray" cx="{x}" cy="{y}" r="29"/><circle class="knob" cx="{x}" cy="{y}" '
                      f'r="24"/><text class="lbl" x="{x}" y="{y - 36}">{name}</text>' + badge(x, y, name), name))
    for k in range(4):
        x = 610 + 104 * k
        s.append(link("KNOB 1", f'<circle class="tray" cx="{x}" cy="100" r="27"/><circle class="knob" cx="{x}" '
                      f'cy="100" r="22"/><text class="lbl" x="{x}" y="66">KNOB {k + 1}</text>' + badge(x, 100, "KNOB 1"),
                      f"KNOB {k + 1}"))
    for name, sub, x in (("OCT-", "SHIFT", 60), ("OCT+", "COPY / OK", 170)):
        s.append(link(name, f'<rect class="btn" x="{x}" y="258" width="90" height="34" rx="8"/>'
                      f'<text class="bl" x="{x + 45}" y="273">{name.replace("-", "−")}</text>'
                      f'<text class="bs" x="{x + 45}" y="286">{sub}</text>' + badge(x + 45, 314, name),
                      f"{name.replace('-', '−')} ({sub})"))
    rows = (["FX", "SCL", "ENV", "LFO", "EDIT", "GLO"], ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"])
    for r, row in enumerate(rows):
        for i, name in enumerate(row):
            x, y = 582 + 63 * i, 174 + 62 * r
            s.append(link(name, f'<rect class="btn" x="{x}" y="{y}" width="54" height="48" rx="8"/>'
                          f'<text class="bl{" rec" if name == "REC" else ""}" x="{x + 27}" y="{y + 20}">{name}</text>'
                          + badge(x + 27, y + 34, name), name))
    x0, y0, w, h = 36, 332, 928, 256
    pitch, kw, kh = (w - 24) / 16, (w - 24) / 16 * 0.78, h * 0.425
    s.append(f'<rect class="tray" x="{x0}" y="{y0}" width="{w}" height="{h}" rx="18"/>')
    keys = []
    for i in range(16):
        cx = x0 + 12 + (i + 0.5) * pitch
        keys.append(f'<rect class="key" x="{cx - kw / 2:.1f}" y="{y0 + h * 0.75 - kh / 2:.1f}" width="{kw:.1f}" '
                    f'height="{kh:.1f}" rx="{kw / 2:.1f}"/><text class="kl" x="{cx:.1f}" y="{y0 + h - 6}">{i + 1}</text>')
    s.append(link("WHITE", "".join(keys) + badge(500, y0 + h * 0.75, "WHITE"), "White keys"))
    after = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]
    names = ["D1", "D2", "D3", "SN", "CY", "CH", "OH", "B1", "B2", "B3", "B4"]
    for grp, lo, hi in (("TRACKS", 0, 7), ("BARS", 7, 11)):
        g, xs = [], []
        for j in range(lo, hi):
            cx = x0 + 12 + (after[j] + 1) * pitch
            xs.append(cx)
            g.append(f'<rect class="key" x="{cx - kw / 2:.1f}" y="{y0 + h * 0.25 - kh / 2:.1f}" width="{kw:.1f}" '
                     f'height="{kh:.1f}" rx="{kw / 2:.1f}"/><text class="kl {"tk" if grp == "TRACKS" else "bk"}" '
                     f'x="{cx:.1f}" y="{y0 + h * 0.5 + 8}">{names[j]}</text>')
        mid = (xs[0] + xs[-1]) / 2
        g.append(f'<path class="brace" d="M{xs[0] - kw / 2:.1f} {y0 + 12} v-6 H{xs[-1] + kw / 2:.1f} v6"/>')
        s.append(link(grp, "".join(g) + badge(mid, y0 + h * 0.25, grp), "Black keys " + ("1–7" if lo == 0 else "8–11")))
    s.append("</svg>")
    rows_html = []
    for name, (nums, what) in PANEL_SEC.items():
        label = {"OCT-": "OCT− (SHIFT)", "OCT+": "OCT+ (COPY)", "KNOB 1": "KNOB 1–4", "WHITE": "White keys",
                 "TRACKS": "Black keys 1–7", "BARS": "Black keys 8–11", "SCREEN": "Screen"}.get(name, name)
        refs = ", ".join(f'<a href="#{sec[n]}">{html.escape(title[n])}</a>' for n in nums)
        rows_html.append(f"<tr><th>{label}</th><td>{html.escape(what)}</td><td>{refs}</td></tr>")
    return (f'<figure class="fm1">{"".join(s)}<figcaption>The FM-1 at a glance. Each control carries the number of '
            'the section that explains it: tap one to go there.</figcaption></figure>'
            '<details class="fm1-key"><summary>Every control and its section</summary><div class="table"><table>'
            '<thead><tr><th>Control</th><th>What it does</th><th>Read</th></tr></thead><tbody>' + "".join(rows_html) +
            "</tbody></table></div></details>")


PAGE = """<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only. Generated from MANUAL.md by web/make_site.py; do not edit. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="The Elixir manual: the LXR-02's drum machine on the M-VAVE FM-1, key by key.">
<link rel="icon" href="../favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="../site.css">
<style>
  /* the manual as an old book: site.css's colours, a page of parchment, serif headings, chapters with openings.
     The text stays plain and large; the theme is in the frame */
  :root {{ --paper: #f3ead8; --leaf: #fcf8ef; --ink: #2a241b; --rule: #d9ccb2; --gild: #a85f00; --well2: #f3ecdf;
           --serif: "Iowan Old Style", "Palatino Linotype", Palatino, "Book Antiqua", "URW Palladio L", Georgia, serif; }}
  @media (prefers-color-scheme: dark) {{
    :root:not([data-theme="light"]) {{ --paper: #0f0d0a; --leaf: #1a1713; --ink: #ece4d3; --rule: #3a332a;
                                       --gild: #f5b04f; --well2: #221e19; }}
  }}
  :root[data-theme="dark"] {{ --paper: #0f0d0a; --leaf: #1a1713; --ink: #ece4d3; --rule: #3a332a; --gild: #f5b04f;
                              --well2: #221e19; }}
  body {{ background: var(--paper); color: var(--ink); }}
  header.site {{ background: var(--bg); }}
  .book {{ display: grid; grid-template-columns: 270px minmax(0, 800px); gap: 40px; justify-content: center;
           padding: 0 16px; max-width: 1180px; margin: 0 auto; }}
  main {{ min-width: 0; margin: 32px 0 80px; padding: 48px 56px 64px; background: var(--leaf);
          border: 1px solid var(--rule); border-radius: 4px;
          box-shadow: 0 1px 0 var(--rule), 0 18px 40px -28px rgba(60, 40, 10, .45),
                      inset 0 0 80px rgba(168, 95, 0, .045); font: 17px/1.7 var(--serif); }}
  main > h1 {{ font: 600 40px/1.15 var(--serif); letter-spacing: .01em; margin: 0 0 6px; text-align: center; }}
  main > h1 + p {{ text-align: center; color: var(--dim); font-style: italic; }}
  h2 {{ font: 600 27px/1.25 var(--serif); margin: 56px 0 14px; padding-bottom: 8px; color: var(--ink);
        border-bottom: 3px double var(--rule); letter-spacing: 0; }}
  h3 {{ font: 600 20px/1.3 var(--serif); margin: 34px 0 8px; color: var(--gild); }}
  h4 {{ font: 600 17px/1.3 var(--serif); margin: 24px 0 6px; }}
  h2, h3, h4, .chapter {{ scroll-margin-top: 16px; }}
  p, li {{ hyphens: auto; }}
  p {{ margin: 0 0 14px; }}
  ul, ol {{ padding-left: 1.4em; margin: 0 0 14px; }}
  li {{ margin-bottom: 5px; }}
  strong {{ font-weight: 650; }}
  hr {{ border: 0; height: 24px; margin: 28px 0; background: none; text-align: center; }}
  hr::after {{ content: "\\2042"; color: var(--rule); font-size: 18px; }}
  a {{ color: var(--gild); }}
  code {{ font: 0.84em ui-monospace, SFMono-Regular, Menlo, monospace; background: var(--well2); padding: 1px 5px;
          border-radius: 3px; }}
  pre {{ background: var(--well2); padding: 12px 14px; overflow-x: auto; border-radius: 4px; border: 1px solid var(--rule); }}
  pre code {{ padding: 0; background: none; }}
  .table, pre, ul, ol, h3, h4 {{ clear: both; }}
  .table {{ overflow-x: auto; margin: 0 0 18px; }}
  table {{ border-collapse: collapse; min-width: 100%;
           font: 14px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", "Helvetica Neue", Arial, sans-serif; }}
  th, td {{ text-align: left; vertical-align: top; padding: 7px 10px; border-bottom: 1px solid var(--rule); }}
  thead th {{ font-weight: 650; border-bottom: 2px solid var(--rule); }}
  tbody tr:nth-child(even) {{ background: color-mix(in srgb, var(--well2) 55%, transparent); }}
  .grid table {{ border: 1px solid var(--rule); table-layout: fixed; }}
  .grid th, .grid td {{ border: 1px solid var(--rule); text-align: center; vertical-align: middle; padding: 6px 4px;
                        font-size: 13px; }}
  .grid th:first-child, .grid td:first-child {{ text-align: left; width: 9em; font-weight: 600; padding-left: 8px; }}
  .grid th:last-child, .grid td:last-child {{ background: var(--well2); }}
  .grid strong {{ display: block; font-size: 15px; }}

  /* a chapter's opening */
  .chapter {{ margin: 88px -56px 8px; padding: 40px 56px 28px; text-align: center; border-top: 1px solid var(--rule);
              background: linear-gradient(var(--well2), transparent 85%); }}
  main > .chapter:first-of-type {{ margin-top: 40px; }}
  .chapter .cnum {{ margin: 0; font: 600 13px/1 var(--serif); letter-spacing: .32em; text-transform: uppercase;
                    color: var(--gild); }}
  .chapter .ctitle {{ margin: 12px 0 6px; font: 600 38px/1.15 var(--serif); }}
  .chapter .cline {{ margin: 0; font-style: italic; color: var(--dim); }}
  .chapter .flourish {{ margin: 16px auto 0; display: flex; align-items: center; gap: 12px; max-width: 220px;
                       font-size: 11px; line-height: 1; color: var(--gild); }}
  .chapter .flourish::before, .chapter .flourish::after {{ content: ""; flex: 1; height: 1px; background: var(--gild);
                                                           opacity: .55; }}
  .chapter + h2 {{ margin-top: 28px; }}
  .chapter + h2 + p::first-letter, .chapter + h2 + h3 + p::first-letter {{
    float: left; font: 600 3.3em/0.82 var(--serif); padding: 6px 8px 0 0; color: var(--gild); }}

  /* the FM-1 at a glance */
  .fm1 {{ margin: 20px 0 10px; }}
  .fm1 svg {{ display: block; width: 100%; height: auto; }}
  .fm1 figcaption {{ font-size: 14px; color: var(--dim); font-style: italic; text-align: center; margin-top: 8px; }}
  .fm1 .body {{ fill: #1d1d1f; }} .fm1 .tray {{ fill: #141415; }} .fm1 .scr {{ fill: #0b0b0c; }}
  .fm1 .glass {{ fill: #000; stroke: #2a2a2d; }}
  .fm1 .knob {{ fill: #2a2a2d; stroke: #3a3a3e; stroke-width: 2; }}
  .fm1 .btn {{ fill: #2a2a2d; stroke: #3a3a3e; stroke-width: 1.5; }}
  .fm1 .key {{ fill: #0c0c0d; stroke: #3a3a3e; stroke-width: 1.5; }}
  .fm1 text {{ font: 600 12px -apple-system, "Helvetica Neue", Arial, sans-serif; text-anchor: middle; letter-spacing: .04em; }}
  .fm1 .lbl {{ fill: #d6d6d8; font-size: 15px; }} .fm1 .bl {{ fill: #d6d6d8; font-size: 12px; }}
  .fm1 .bs {{ fill: #8d8d92; font-size: 9.5px; }} .fm1 .rec {{ fill: #ff453a; }}
  .fm1 .kl {{ fill: #8d8d92; font-size: 11px; }} .fm1 .tk {{ fill: #c9a46a; }} .fm1 .bk {{ fill: #7fb0d8; }}
  .fm1 .brace {{ fill: none; stroke: #5a5a5e; stroke-width: 1.5; }}
  .fm1 .bdg {{ fill: #f0a43a; }} .fm1 .bdgt {{ fill: #1d1b18; font-size: 12px; font-weight: 700; letter-spacing: 0; }}
  .fm1 a.ctl {{ cursor: pointer; }}
  .fm1 a.ctl:hover .bdg, .fm1 a.ctl:focus .bdg {{ fill: #f8d394; }}
  .fm1 a.ctl:hover .btn, .fm1 a.ctl:hover .knob, .fm1 a.ctl:hover .key, .fm1 a.ctl:focus .btn {{ stroke: #f0a43a; }}
  .fm1 a.ctl:focus {{ outline: none; }}
  .fm1-key {{ margin: 0 0 22px; }}
  .fm1-key summary {{ cursor: pointer; color: var(--gild); font-weight: 600; margin-bottom: 8px; }}
  .fm1-key th {{ white-space: nowrap; }}

  /* a heading's link mark: shown on hover (always, faintly, on touch); a click copies the section's address */
  .hl {{ display: inline-flex; vertical-align: middle; margin-left: .3em; padding: 2px; border-radius: 4px; color: var(--dim);
         opacity: 0; transition: opacity .15s; text-decoration: none; position: relative; }}
  .hl svg {{ width: .62em; height: .62em; min-width: 13px; min-height: 13px; }}
  h2:hover .hl, h3:hover .hl, h4:hover .hl, .hl:focus-visible {{ opacity: 1; }}
  .hl:hover {{ color: var(--gild); }}
  @media (hover: none) {{ .hl {{ opacity: .45; }} }}
  .hl.ok {{ opacity: 1; color: var(--gild); }}
  .hl.ok::after {{ content: "Link copied"; position: absolute; left: 100%; top: 50%; transform: translateY(-50%);
                   margin-left: 6px; padding: 2px 8px; border-radius: 4px; background: var(--ink); color: var(--leaf);
                   font: 600 12px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif; white-space: nowrap; }}

  /* the signal-flow diagrams (docs/img, tools/flow_svg.py) */
  figure.flow {{ margin: 22px 0 26px; text-align: center; }}
  figure.flow img {{ display: block; width: 100%; max-width: 480px; height: auto; margin: 0 auto; }}
  figure.flow figcaption {{ font-size: 14px; color: var(--dim); font-style: italic; margin-top: 8px; }}

  /* the PDF's link (under the title, and atop the side bar) */
  .pdf {{ text-align: center; margin: 18px 0 0; font: 600 14px/1.4 -apple-system, BlinkMacSystemFont, "Segoe UI", Arial,
          sans-serif; }}
  aside .pdf {{ text-align: left; margin: 0 0 14px 8px; }}
  aside .pdf a {{ display: inline; padding: 0; border: 0; color: var(--gild); }}

  /* the side bar: the book's contents */
  aside {{ position: sticky; top: 0; align-self: start; max-height: 100vh; overflow-y: auto; padding: 32px 0;
           font: 14px/1.4 -apple-system, BlinkMacSystemFont, "Segoe UI", "Helvetica Neue", Arial, sans-serif; }}
  aside summary {{ display: none; }}
  aside .title {{ font: 600 13px/1 var(--serif); letter-spacing: .28em; text-transform: uppercase; color: var(--gild);
                  margin: 0 0 14px 8px; }}
  aside ul {{ list-style: none; margin: 0 0 4px; padding: 0; }}
  aside li {{ margin: 0; }}
  aside .chap {{ margin: 16px 0 4px; }}
  aside .chap a {{ font: 600 15px/1.3 var(--serif); color: var(--ink); border: 0; padding: 2px 8px; }}
  aside .chap span {{ display: inline-block; min-width: 2em; color: var(--gild); font-variant: small-caps; }}
  aside a {{ display: block; padding: 4px 8px; text-decoration: none; color: var(--dim); border-left: 2px solid
             transparent; }}
  aside a:hover {{ color: var(--ink); }}
  aside a.on {{ color: var(--ink); border-left-color: var(--gild); font-weight: 600; }}
  aside li ul {{ display: none; margin: 2px 0 6px; }}
  aside li.open > ul {{ display: block; }}
  aside li ul a {{ padding-left: 20px; font-size: 13px; }}
  aside li ul a.on {{ font-weight: 500; }}
  @media (max-width: 960px) {{
    .book {{ display: block; padding: 0; }}
    aside {{ position: sticky; top: 0; z-index: 1; background: var(--paper); padding: 8px 16px; max-height: 70vh;
             border-bottom: 1px solid var(--rule); }}
    aside summary {{ display: list-item; cursor: pointer; font-weight: 600; padding: 4px 0; }}
    aside .title {{ display: none; }}
    main {{ margin: 0; border: 0; border-radius: 0; padding: 28px 16px 56px; box-shadow: none; font-size: 17px; }}
    main > h1 {{ font-size: 32px; }}
    .chapter {{ margin: 64px -16px 8px; padding: 32px 16px 22px; }}
    .chapter .ctitle {{ font-size: 30px; }}
    h2, h3, h4, .chapter {{ scroll-margin-top: 56px; }}
    aside .pdf {{ display: none; }}
  }}
  /* printed (and the PDF, make_site's print_pdf): the page alone, light, each chapter from a new page */
  @media print {{
    :root:not([data-theme]), :root[data-theme] {{ --paper: #fff; --leaf: #fff; --ink: #1d1b18; --rule: #d9ccb2;
      --gild: #8a4e00; --well2: #f3ecdf; --bg: #fff; --fg: #1d1b18; --dim: #5e5649; color-scheme: light; }}
    @page {{ margin: 16mm 15mm; }}
    header.site, aside, .hl, .pdf, .fm1-key {{ display: none !important; }}
    .book {{ display: block; padding: 0; max-width: none; }}
    main {{ margin: 0; padding: 0; border: 0; box-shadow: none; font-size: 11pt; line-height: 1.55; }}
    .chapter {{ break-before: page; margin: 0 0 8px; padding: 24px 0 18px; background: none; border: 0; }}
    h2, h3, h4 {{ break-after: avoid; }}
    figure, tr, pre {{ break-inside: avoid; }}
    a {{ color: inherit; text-decoration: none; }}
    tbody tr:nth-child(even) {{ background: var(--well2); -webkit-print-color-adjust: exact; print-color-adjust: exact; }}
  }}
</style>
</head>
<body>
{nav}
<div class="book">
<aside><details id="toc" open><summary>Contents</summary>
{pdf_side}<p class="title">Contents</p>
{toc}
</details></aside>
<main>
{body}
</main>
</div>
<script>
// the side bar follows the reading: the section in view marked, its subsections shown; on a phone it folds away
(() => {{
  const toc = document.getElementById("toc"), narrow = matchMedia("(max-width: 960px)");
  if (narrow.matches) toc.open = false;
  const links = [...toc.querySelectorAll("li a")];
  const heads = links.map((a) => document.getElementById(a.hash.slice(1))).filter(Boolean);
  toc.addEventListener("click", (e) => {{ if (e.target.closest("a") && narrow.matches) toc.open = false; }});
  let last = null;
  function mark() {{
    let cur = heads[0];
    for (const h of heads) if (h.getBoundingClientRect().top < innerHeight / 3) cur = h; else break;
    if (cur === last) return;
    last = cur;
    links.forEach((a) => a.classList.toggle("on", a.hash === "#" + cur.id));
    toc.querySelectorAll("li.open").forEach((li) => li.classList.remove("open"));
    const a = links.find((a) => a.hash === "#" + cur.id);
    let li = a && a.closest("li");
    while (li) {{ li.classList.add("open"); li = li.parentElement.closest("li"); }}
    if (a && !narrow.matches) a.scrollIntoView({{ block: "nearest" }});
  }}
  addEventListener("scroll", mark, {{ passive: true }});
  // a heading's link mark: copies the section's address (and puts it in the address bar)
  document.querySelector("main").addEventListener("click", async (e) => {{
    const a = e.target.closest("a.hl");
    if (!a) return;
    e.preventDefault();
    const url = location.origin + location.pathname + a.hash;
    try {{ history.replaceState(null, "", a.hash); }} catch {{}}
    try {{ await navigator.clipboard.writeText(url); }} catch {{ location.hash = a.hash; return; }}
    a.classList.add("ok");
    clearTimeout(a.okt);
    a.okt = setTimeout(() => a.classList.remove("ok"), 1500);
  }});
  addEventListener("hashchange", mark);
  mark();
}})();
</script>
</body>
</html>
"""


# the manual's PDF: the manual page printed by headless Chrome or Chromium (its print styles: the page alone,
# light, a chapter a page), with an outline from its headings. No browser: no PDF and no link, with a warning
def chrome():
    for c in (os.environ.get("CHROME"), "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
              "/Applications/Chromium.app/Contents/MacOS/Chromium", shutil.which("google-chrome"),
              shutil.which("google-chrome-stable"), shutil.which("chromium"), shutil.which("chromium-browser")):
        if c and Path(c).exists():
            return c
    return None


def print_pdf(page, pdf):
    exe = chrome()
    if not exe:
        print("site: no Chrome or Chromium (CHROME=...): the manual has no PDF", file=sys.stderr)
        return False
    with tempfile.TemporaryDirectory() as prof:
        subprocess.run([exe, "--headless", "--disable-gpu", "--no-pdf-header-footer", "--generate-pdf-document-outline",
                        f"--user-data-dir={prof}", "--allow-file-access-from-files", "--virtual-time-budget=10000",
                        f"--print-to-pdf={pdf.resolve()}", page.resolve().as_uri()],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=180)
    if not pdf.exists() or pdf.stat().st_size < 10000:
        print(f"site: printing the manual to {pdf.name} failed: no PDF", file=sys.stderr)
        pdf.unlink(missing_ok=True)
        return False
    return True


def files_page(entries):
    li = "".join(f'<li><a href="{n}">{n}</a> <span>{d}</span></li>' for n, d in entries)
    return ('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
            "<title>Elixir files</title><style>body{font:16px/1.6 -apple-system,Arial,sans-serif;max-width:640px;"
            "margin:0 auto;padding:32px 16px}a{color:inherit}span{color:#777;font-size:14px}</style>"
            '<h1>Elixir files</h1><ul>' + li + '</ul><p><a href="../">Elixir</a> · <a href="../install/">Install</a></p>\n')


def main(pkg, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product, ver = product_of(raw), version()
    if not re.fullmatch(r"FM-1_359\d{6}", product):   # (0.22: Elixir's own, tools/build.py product_for)
        raise SystemExit(f"{pkg}: identity {product!r} is not Elixir's (FM-1_359XXYYZZ)")
    if b"FELUCCA-LOADER-1" not in raw:              # Felucca's own loader: never publish a package with vendor files
        raise SystemExit(f"{pkg}: no Felucca loader in it "
                         "(a package patched from an official one carries vendor files)")
    emu = ROOT / "build/emu"
    if not (emu / "elixir.wasm").exists():
        raise SystemExit("no build/emu: run web/emu/build.sh first")
    if out.exists():
        shutil.rmtree(out)
    fw, man = out / "firmware", out / "manual"
    for d in (fw, man):
        d.mkdir(parents=True)

    name = f"elixir-{ver}.fwsc"
    shutil.copy(pkg, fw / name)
    repo = f' · <a href="{REPO}" target="_blank" rel="noopener">GitHub</a>' if REPO else ""

    # 0.14: the front page at the root, the install page at install/
    shutil.copy(ROOT / "web/site.css", out / "site.css")
    shutil.copytree(ROOT / "web/shots", out / "shots")
    shutil.copy(ROOT / "web/cheatsheet.pdf", out / "cheatsheet.pdf")
    (out / "favicon.svg").write_text(favicon(), encoding="utf-8")
    front = (ROOT / "web/index.html").read_text(encoding="utf-8")
    for mark, text in (("<!--NAV-->", nav(0)), ("<!--WORD-->", word_svg()), ("<!--POTION-->", potion_svg()),
                       ("<!--VERSION-->", html.escape(ver)), ("<!--REPO-->", repo)):
        if front.count(mark) != 1:
            raise SystemExit(f"web/index.html must contain {mark} once")
        front = front.replace(mark, text)
    (out / "index.html").write_text(front, encoding="utf-8")

    page = (ROOT / "web/install.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((ROOT / "web" / f).read_text(encoding="utf-8"))
                    for f in ("fm1pkg.js", "fm1ota.js", "install.js"))
    meta = json.dumps({"version": ver, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/", "<!--NAV-->"):
        if page.count(mark) != 1:
            raise SystemExit(f"web/install.html must contain {mark} once")
    page = page.replace("<!--REPO-->", repo).replace("<!--NAV-->", nav(1, "install/"))
    (out / "install").mkdir()
    (out / "install/index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")

    (out / "library").mkdir()                        # 0.7: the library page (backup, restore, LXR kits)
    (out / "library/index.html").write_text((ROOT / "web/library.html").read_text(encoding="utf-8")
                                            .replace("<!--NAV-->", nav(1, "library/")), encoding="utf-8")
    shutil.copy(ROOT / "web/library.js", out / "library/library.js")

    shutil.copytree(emu, out / "try")
    t = (out / "try/index.html").read_text(encoding="utf-8")
    for mark in ("</head>", "<body>"):
        if t.count(mark) != 1:
            raise SystemExit(f"build/emu/index.html must contain {mark} once")
    t = t.replace("</head>", NAV_CSS + "\n</head>").replace("<body>", "<body>\n" + nav(1, "try/"))
    (out / "try/index.html").write_text(t, encoding="utf-8")
    (out / "try/mobile.html").unlink(missing_ok=True)    # (0.16: the phone page is mobile/, on try/'s files)
    (out / "mobile").mkdir()
    m = (ROOT / "web/emu/mobile.html").read_text(encoding="utf-8")
    m = m.replace("@VERSION@", ver).replace("@BASE@", "../try/").replace("@SITE@", "../")
    (out / "mobile/index.html").write_text(m, encoding="utf-8")
    shutil.copy(ROOT / "web/emu/apple-touch-icon.png", out / "mobile/apple-touch-icon.png")   # (0.25: the home-screen icon)

    md = re.sub(r"^## Contents\n.*?^---\n", "", (ROOT / "MANUAL.md").read_text(encoding="utf-8"), flags=re.M | re.S)
    title, body, heads = markdown(md)                # (the side bar replaces the manual's own contents)
    if body.count("<!--PANEL-->") > 1:
        raise SystemExit("MANUAL.md: <!-- panel --> more than once")
    body = chapters(body.replace("<!--PANEL-->", panel_svg(heads)), heads)
    shutil.copytree(ROOT / "docs/img", man / "img")   # (the signal-flow diagrams MANUAL.md shows)

    def manual_page(pdf):                            # pdf: the PDF's name, or None (no link)
        side = f'<p class="pdf"><a href="{pdf}">The manual as a PDF</a></p>' if pdf else ""
        b = body.replace("<hr>", f'<p class="pdf"><a href="{pdf}">Download the manual as a PDF</a></p>\n<hr>', 1) \
            if pdf else body                         # (after the opening paragraphs)
        (man / "index.html").write_text(PAGE.format(title=html.escape(title), body=b, toc=toc(heads), pdf_side=side,
                                                    nav=nav(1, "manual/")), encoding="utf-8")

    manual_page(None)                                # (printed without the link: the print styles hide it anyway)
    pdf = f"elixir-manual-{ver}.pdf"
    if print_pdf(man / "index.html", man / pdf):
        manual_page(pdf)

    for f in ("LICENSE", "NOTICE.md", "MANUAL.md"):
        shutil.copy(ROOT / f, fw / f)
    shutil.copytree(ROOT / "LICENSES", fw / "LICENSES")
    lic = sorted(p.name for p in (fw / "LICENSES").glob("*.txt"))
    (fw / "LICENSES/index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Elixir licences</title><h1>Licence texts</h1><ul>' +
        "".join(f'<li><a href="{n}">{n}</a></li>' for n in lic) + '</ul><p><a href="../LICENSE">LICENSE (GPL-3.0)</a>'
        ' · <a href="../NOTICE.md">NOTICE.md</a></p>\n', encoding="utf-8")
    src = f"elixir-{ver}-src.tar.gz"
    skip = lambda ti: None if re.search(r"(__pycache__|\.dSYM|\.DS_Store|docs/TO_DO[^/]*\.md|"
                                        r"docs/TECHNICAL_DOCUMENTATION\.md)", ti.name) else ti   # (private notes)
    with tarfile.open(fw / src, "w:gz") as tf:
        for f in SRC:
            tf.add(ROOT / f, arcname=f"elixir-{ver}/{f}", filter=skip)
    (fw / "index.html").write_text(files_page([
        (name, f"the firmware package ({len(raw)} B, identity {product})"),
        (src, "the complete source (GPL-3.0)"),
        ("MANUAL.md", "the manual as Markdown"),
        ("LICENSE", "GPL-3.0"), ("NOTICE.md", "what Elixir is built from"), ("LICENSES/", "the other licence texts")]),
        encoding="utf-8")
    (out / ".nojekyll").write_text("")              # GitHub Pages: serve the files as they are
    print(f"site: {out}: index.html, install/, library/, try/, manual/, firmware/{name} ({len(raw)} B, {product}), firmware/{src}")


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) > 2:
        sys.exit("usage: web/make_site.py [PACKAGE.fwsc] [OUT_DIR]")
    main(a[0] if a else ROOT / "build/elixir.fwsc", a[1] if len(a) > 1 else ROOT / "build/site")
