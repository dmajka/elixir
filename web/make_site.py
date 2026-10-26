#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Kettle's site (GitHub Pages or any static host), after Felucca's web/make_site.py (Copyright (C) 2026 Leo Kuroshita
# (@kurogedelic), Hügelton Instruments):
#
#   index.html                  the install page: web/install.html with fm1pkg.js, fm1ota.js and the package inlined
#   try/                        the emulator (build/emu: run web/emu/build.sh first)
#   library/                    backup, restore and LXR-02 kit import over Web MIDI (web/library.html, library.js)
#   manual/index.html           MANUAL.md as a page
#   firmware/                   kettle-VER.fwsc, kettle-VER-src.tar.gz (the source, as the GPL asks), MANUAL.md,
#                               LICENSE, NOTICE.md, LICENSES/, and a page listing them
#
#   web/make_site.py [PACKAGE.fwsc] [OUT_DIR]      (default: build/kettle.fwsc build/site)
import html
import json
import re
import shutil
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = "https://github.com/dmajka/kettle"   # the source repository's URL, once public: the pages link it
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
SRC = ["build.sh", "LICENSE", "LICENSES", "NOTICE.md", "README.md", "ROADMAP.md", "MANUAL.md",
       "firmware", "tools", "tests", "web", "assets"]


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def version():
    m = re.search(r'#define KETTLE_VERSION "([^"]+)"', (ROOT / "firmware/src/kettle.c").read_text())
    return m[1]


# the manual: the Markdown MANUAL.md uses (headings, paragraphs, lists, tables, fenced code, rules, emphasis, code,
# links), with GitHub's heading anchors so its links work. A line <!-- grid --> (hidden on GitHub) makes the table
# after it a grid: every cell boxed and centred, the last column shaded (the remote map's appendix)
def slug(text):
    return re.sub(r"[^\w\- ]", "", text.lower()).replace(" ", "-")


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
    lines, out, i, title, heads, grid = md.split("\n"), [], 0, "Kettle manual", [], False
    while i < len(lines):
        ln = lines[i]
        if ln.strip() == "<!-- grid -->":
            grid = True
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
            out.append(f'<h{n} id="{slug(text)}">{inline(text)}</h{n}>')
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
            while i < len(lines) and lines[i].strip() and not re.match(r"(#|```|\||---$|\d+\. |[-*] )", lines[i]):
                para.append(lines[i].strip())
                i += 1
            out.append("<p>" + inline(" ".join(para)) + "</p>")
    return title, "\n".join(out), heads


def toc(heads):
    """the side bar: the sections, each with its subsections (shown for the section being read)"""
    out, open_ul = [], False
    for n, hid, text in heads:
        if n == 2:
            if open_ul:
                out.append("</ul>")
                open_ul = False
            if out:
                out.append("</li>")
            out.append(f'<li><a href="#{hid}">{inline(text)}</a>')
        else:
            if not open_ul:
                out.append("<ul>")
                open_ul = True
            out.append(f'<li><a href="#{hid}">{inline(text)}</a></li>')
    out.append("</ul></li>" if open_ul else "</li>")
    return "<ul>" + "".join(out) + "</ul>"


PAGE = """<!doctype html>
<!-- SPDX-License-Identifier: GPL-3.0-only. Generated from MANUAL.md by web/make_site.py; do not edit. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
  :root {{ --bg: #fff; --fg: #111; --dim: #666; --line: #ddd; --code: #f3f3f3; }}
  @media (prefers-color-scheme: dark) {{
    :root:not([data-theme="light"]) {{ --bg: #0b0b0b; --fg: #eee; --dim: #999; --line: #333; --code: #1b1b1b; }}
  }}
  :root[data-theme="dark"] {{ --bg: #0b0b0b; --fg: #eee; --dim: #999; --line: #333; --code: #1b1b1b; }}
  body {{ margin: 0; background: var(--bg); color: var(--fg);
         font: 16px/1.65 -apple-system, BlinkMacSystemFont, "Helvetica Neue", Arial, sans-serif; }}
  .wrap {{ display: grid; grid-template-columns: 280px minmax(0, 760px); gap: 48px; justify-content: center;
           padding: 0 16px; }}
  main {{ padding: 32px 0 80px; min-width: 0; }}
  aside {{ position: sticky; top: 0; align-self: start; max-height: 100vh; overflow-y: auto; padding: 32px 0 32px;
           font-size: 14px; line-height: 1.4; }}
  aside summary {{ display: none; }}
  aside .title {{ font-weight: 600; margin: 0 0 12px; }}
  aside ul {{ list-style: none; margin: 0; padding: 0; }}
  aside li {{ margin: 0; }}
  aside a {{ display: block; padding: 4px 8px; text-decoration: none; color: var(--dim); border-left: 2px solid
             transparent; }}
  aside a:hover {{ color: var(--fg); }}
  aside a.on {{ color: var(--fg); border-left-color: var(--fg); font-weight: 600; }}
  aside li ul {{ display: none; margin: 2px 0 6px; }}
  aside li.open > ul {{ display: block; }}
  aside li ul a {{ padding-left: 20px; font-size: 13px; }}
  aside li ul a.on {{ font-weight: 500; }}
  @media (max-width: 900px) {{
    .wrap {{ display: block; }}
    aside {{ position: sticky; top: 0; z-index: 1; background: var(--bg); padding: 8px 0; max-height: 70vh;
             border-bottom: 1px solid var(--line); }}
    aside summary {{ display: list-item; cursor: pointer; font-weight: 600; padding: 4px 0; }}
    aside .title {{ display: none; }}
    h2, h3 {{ scroll-margin-top: 56px; }}
  }}
  nav.top {{ font-size: 14px; margin-bottom: 24px; }}
  nav.top a {{ margin-right: 16px; }}
  a {{ color: inherit; }}
  h1 {{ font-size: 30px; margin: 0 0 16px; }}
  h2 {{ font-size: 22px; margin: 40px 0 12px; }}
  h3 {{ font-size: 17px; margin: 28px 0 8px; }}
  h2, h3 {{ scroll-margin-top: 16px; }}
  hr {{ border: 0; border-top: 1px solid var(--line); margin: 32px 0; }}
  code {{ font: 14px ui-monospace, SFMono-Regular, Menlo, monospace; background: var(--code); padding: 1px 4px;
          border-radius: 3px; }}
  pre {{ background: var(--code); padding: 12px; overflow-x: auto; border-radius: 4px; }}
  pre code {{ padding: 0; background: none; }}
  .table {{ overflow-x: auto; margin: 0 0 16px; }}
  table {{ border-collapse: collapse; font-size: 14px; min-width: 100%; }}
  th, td {{ text-align: left; vertical-align: top; padding: 6px 10px; border-bottom: 1px solid var(--line); }}
  th {{ font-weight: 600; }}
  .grid table {{ border: 1px solid var(--line); table-layout: fixed; }}
  .grid th, .grid td {{ border: 1px solid var(--line); text-align: center; vertical-align: middle; padding: 6px 4px;
                        font-size: 13px; }}
  .grid th:first-child, .grid td:first-child {{ text-align: left; width: 9em; font-weight: 600; padding-left: 8px; }}
  .grid th:last-child, .grid td:last-child {{ background: var(--code); }}
  .grid strong {{ display: block; font-size: 15px; }}
  li {{ margin-bottom: 4px; }}
</style>
</head>
<body>
<div class="wrap">
<aside><details id="toc" open><summary>Contents</summary>
<p class="title">Contents</p>
{toc}
</details></aside>
<main>
<nav class="top"><a href="../">Install</a> <a href="../try/">Try in the browser</a>
  <a href="../firmware/">Files</a></nav>
{body}
</main>
</div>
<script>
// the side bar follows the reading: the section in view marked, its subsections shown; on a phone it folds away
(() => {{
  const toc = document.getElementById("toc"), narrow = matchMedia("(max-width: 900px)");
  if (narrow.matches) toc.open = false;
  const links = [...toc.querySelectorAll("a")];
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
  addEventListener("hashchange", mark);
  mark();
}})();
</script>
</body>
</html>
"""


def files_page(entries):
    li = "".join(f'<li><a href="{n}">{n}</a> <span>{d}</span></li>' for n, d in entries)
    return ('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
            "<title>Kettle files</title><style>body{font:16px/1.6 -apple-system,Arial,sans-serif;max-width:640px;"
            "margin:0 auto;padding:32px 16px}a{color:inherit}span{color:#777;font-size:14px}</style>"
            '<h1>Kettle files</h1><ul>' + li + '</ul><p><a href="../">Back to the install page</a></p>\n')


def main(pkg, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product, ver = product_of(raw), version()
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not an alternative-firmware package (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # Felucca's own loader: never publish a package with vendor files
        raise SystemExit(f"{pkg}: no Felucca loader in it "
                         "(a package patched from an official one carries vendor files)")
    emu = ROOT / "build/emu"
    if not (emu / "kettle.wasm").exists():
        raise SystemExit("no build/emu: run web/emu/build.sh first")
    # 0.7: never publish a build with LXR-02 kits built in (KETTLE_KITS_DIR: they aren't Kettle's to give away). Its
    # version ends in "+K", and the splash's string carries it into the app image and the emulator.
    private = re.compile(rb"unofficial port, [^\0]*\+K\0")
    for f in (pkg.with_name("kettle.bin"), emu / "kettle.wasm"):
        if f.exists() and private.search(f.read_bytes()):
            raise SystemExit(f"{f}: built with KETTLE_KITS_DIR (LXR-02 kits): rebuild without it before publishing")
    if out.exists():
        shutil.rmtree(out)
    fw, man = out / "firmware", out / "manual"
    for d in (fw, man):
        d.mkdir(parents=True)

    name = f"kettle-{ver}.fwsc"
    shutil.copy(pkg, fw / name)
    page = (ROOT / "web/install.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((ROOT / "web" / f).read_text(encoding="utf-8")) for f in ("fm1pkg.js", "fm1ota.js"))
    meta = json.dumps({"version": ver, "product": product, "pkg": "firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"web/install.html must contain {mark} once")
    repo = f' · <a href="{REPO}" target="_blank" rel="noopener">GitHub</a>' if REPO else ""
    page = page.replace("<!--REPO-->", repo)
    (out / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")

    (out / "library").mkdir()                        # 0.7: the library page (backup, restore, LXR kits)
    shutil.copy(ROOT / "web/library.html", out / "library/index.html")
    shutil.copy(ROOT / "web/library.js", out / "library/library.js")

    shutil.copytree(emu, out / "try")
    t = (out / "try/index.html").read_text(encoding="utf-8")
    back = '<footer>\n  <a href="../">Install Kettle</a> · <a href="../manual/">Manual</a><br>\n'
    t = t.replace("<footer>\n", back, 1)
    (out / "try/index.html").write_text(t, encoding="utf-8")

    md = re.sub(r"^## Contents\n.*?^---\n", "", (ROOT / "MANUAL.md").read_text(encoding="utf-8"), flags=re.M | re.S)
    title, body, heads = markdown(md)                # (the side bar replaces the manual's own contents)
    (man / "index.html").write_text(PAGE.format(title=html.escape(title), body=body, toc=toc(heads)),
                                    encoding="utf-8")

    for f in ("LICENSE", "NOTICE.md", "MANUAL.md"):
        shutil.copy(ROOT / f, fw / f)
    shutil.copytree(ROOT / "LICENSES", fw / "LICENSES")
    lic = sorted(p.name for p in (fw / "LICENSES").glob("*.txt"))
    (fw / "LICENSES/index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Kettle licences</title><h1>Licence texts</h1><ul>' +
        "".join(f'<li><a href="{n}">{n}</a></li>' for n in lic) + '</ul><p><a href="../LICENSE">LICENSE (GPL-3.0)</a>'
        ' · <a href="../NOTICE.md">NOTICE.md</a></p>\n', encoding="utf-8")
    src = f"kettle-{ver}-src.tar.gz"
    skip = lambda ti: None if re.search(r"(__pycache__|\.dSYM|\.DS_Store)", ti.name) else ti
    with tarfile.open(fw / src, "w:gz") as tf:
        for f in SRC:
            tf.add(ROOT / f, arcname=f"kettle-{ver}/{f}", filter=skip)
    (fw / "index.html").write_text(files_page([
        (name, f"the firmware package ({len(raw)} B, identity {product})"),
        (src, "the complete source (GPL-3.0)"),
        ("MANUAL.md", "the manual as Markdown"),
        ("LICENSE", "GPL-3.0"), ("NOTICE.md", "what Kettle is built from"), ("LICENSES/", "the other licence texts")]),
        encoding="utf-8")
    (out / ".nojekyll").write_text("")              # GitHub Pages: serve the files as they are
    print(f"site: {out}: index.html, try/, manual/, firmware/{name} ({len(raw)} B, {product}), firmware/{src}")


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) > 2:
        sys.exit("usage: web/make_site.py [PACKAGE.fwsc] [OUT_DIR]")
    main(a[0] if a else ROOT / "build/kettle.fwsc", a[1] if len(a) > 1 else ROOT / "build/site")
