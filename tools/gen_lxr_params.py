#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: extract the LXR firmware's parameter model into firmware/src/lxr_params.h.
#
#   tools/gen_lxr_params.py LXR_SRC [OUT]
#
# From the LXR-02 firmware sources (GPL-3.0-only): the parameter numbers (Parameters.h ParamEnums: a kit file is
# these values in this order, and MIDI CC n / NRPN n address them), their value types (menu.c parameter_dtypes),
# their names (menu.c valueNames, MenuText.h), the edit pages of every voice (menuPages.h) and the modulation target
# list (Cc2Text.c modTargets: kits store LFO, velocity and automation targets as indexes into it). The output is
# committed; run this again only if the LXR's parameter model changes.
import re
import sys
from pathlib import Path


def strip(s):
    """C comments out (one pass, so a // inside /* */ or the reverse is no trouble), string literals kept, the
    LXR's USE_CODEC_EQ blocks (off in its build) and other preprocessor lines but #define out"""
    out, i, n = [], 0, len(s)
    while i < n:
        if s.startswith("/*", i):
            j = s.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
        elif s.startswith("//", i):
            j = s.find("\n", i)
            i = n if j < 0 else j
        elif s[i] == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        else:
            out.append(s[i])
            i += 1
    s = "".join(out)
    s = re.sub(r"#if\s+USE_CODEC_EQ.*?#endif", " ", s, flags=re.S)
    return re.sub(r"^\s*#(?!define).*$", " ", s, flags=re.M)


def braces(src, start):
    """the text inside the {...} that starts at src[start:] (the first '{' after it)"""
    j = src.index("{", start)
    depth, k = 0, j
    while True:
        if src[k] == "{":
            depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                return src[j + 1:k]
        k += 1


def enum_values(src, name, known=None):
    """the members of `enum name {..}` with their values (C rules: = expr, else previous + 1)"""
    body = braces(src, re.search(r"enum\s+" + name + r"\b", src).start())
    vals, cur = {}, -1
    env = dict(known or {})
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        m = re.match(r"(\w+)\s*(?:=\s*(.+))?$", item, re.S)
        n, expr = m.group(1), m.group(2)
        if expr is not None:
            cur = eval(re.sub(r"\b([A-Za-z_]\w*)\b", lambda mm: str(env[mm.group(1)]), expr.strip()))
        else:
            cur += 1
        vals[n] = cur
        env[n] = cur
    return vals


def main():
    lxr = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[1] / "firmware/src/lxr_params.h"
    core = lxr / "Core"
    par_h = strip((core / "Inc/Parameters.h").read_text())
    menu_h = strip((core / "Inc/menu/menu.h").read_text())
    menu_c = strip((core / "Src/menu/menu.c").read_text())
    pages_h = strip((core / "Inc/menu/menuPages.h").read_text())
    text_h = strip((core / "Inc/menu/MenuText.h").read_text())
    cc2 = strip((core / "Src/menu/Cc2Text.c").read_text())

    P = enum_values(par_h, "ParamEnums")
    T = enum_values(menu_h, "NamesEnum")
    SH = enum_values(menu_h, "shortNamesEnum")
    CA = enum_values(menu_h, "catNamesEnum")
    LO = enum_values(menu_h, "longNamesEnum")
    DT = enum_values(menu_h, "Datatypes")
    MENU = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define\s+(MENU_\w+)\s+(\d+)", text_h)}

    nparams = P["NUM_PARAMS"]
    order = sorted(((v, k) for k, v in P.items() if not k.startswith("NUM_")), key=lambda t: t[0])
    names = {}
    for k, v in P.items():                               # (aliases: the first PAR_ name a value has)
        if k.startswith("PAR_") and k != "PAR_BEGINNING_OF_GLOBALS":
            names.setdefault(v, k)

    # parameter_dtypes: one entry per parameter, DTYPE_x | (MENU_y << 8)
    dt_body = braces(menu_c, menu_c.index("parameter_dtypes[NUM_PARAMS]"))
    dtypes = []
    for e in [x.strip() for x in dt_body.split(",") if x.strip()]:
        m = re.match(r"(DTYPE_\w+)(?:\s*\|\s*\(\s*(MENU_\w+)\s*<<\s*8\s*\))?", e)
        dtypes.append((DT[m.group(1)], MENU[m.group(2)] if m.group(2) else 0))
    if len(dtypes) != nparams:
        raise SystemExit(f"parameter_dtypes: {len(dtypes)} entries, NUM_PARAMS {nparams}")

    # valueNames: TEXT -> (short, category, long)
    vn_body = braces(menu_c, menu_c.index("valueNames[NUM_NAMES]"))
    vnames = [(SH[a], CA[b], LO[c]) for a, b, c in re.findall(r"\{\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\}", vn_body)]
    if len(vnames) != T["NUM_NAMES"]:
        raise SystemExit(f"valueNames: {len(vnames)} of {T['NUM_NAMES']}")

    def strs(arr_name):
        b = braces(text_h, text_h.index(arr_name))
        return re.findall(r'\{\s*"([^"]*)"\s*\}', b)
    shortn, catn, longn = strs("shortNames[][4]"), strs("catNames[][16]"), strs("longNames[][16]")

    # menuPages: [page][subpage] = 8 TEXT + 8 PAR
    pg_body = braces(pages_h, pages_h.index("menuPages[NUM_PAGES][NUM_SUB_PAGES]"))
    pages = []
    depth, cur, page = 0, "", None
    for ch in pg_body:
        if ch == "{":
            depth += 1
            if depth == 1:
                page = []
            cur = ""
        elif ch == "}":
            if depth == 2:
                toks = [t for t in re.findall(r"\w+", cur)]
                page.append(toks)
            depth -= 1
            if depth == 0:
                pages.append(page)
        elif depth == 2:
            cur += ch
    def sub(toks):
        toks = toks + ["TEXT_EMPTY"] * (8 - len(toks[:8])) if len(toks) < 16 else toks
        t = [T.get(x, 0) for x in toks[:8]]
        p = [P.get(x, 0) for x in toks[8:16]] + [0] * (8 - len(toks[8:16]))
        return t, p

    # modTargets: {TEXT, PAR}
    mt_body = braces(cc2, cc2.index("modTargets[]"))
    mods = [(T[a], P[b]) for a, b in re.findall(r"\{\s*(TEXT_\w+)\s*,\s*(PAR_\w+)\s*\}", mt_body)]
    off_body = braces(cc2, cc2.index("modTargetVoiceOffsets[6]"))
    offs = [(int(a), int(b)) for a, b in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", off_body)]

    L = ["/* SPDX-License-Identifier: GPL-3.0-only",
         " * Copyright (C) 2012-2026 Erica Synths (the parameter model of the LXR firmware: Parameters.h, menu.c,",
         " * menuPages.h, MenuText.h, Cc2Text.c). Generated by tools/gen_lxr_params.py; do not edit. */",
         "#pragma once", "#include <stdint.h>", "", "enum {"]
    for v in range(nparams + 1):
        if v in names:
            L.append(f"    {names[v]} = {v},")
    for k, v in sorted(P.items(), key=lambda t: t[1]):
        if names.get(v) != k and not k.startswith("NUM_") and k != "END_OF_SOUND_PARAMETERS":
            L.append(f"    {k} = {v},   /* alias */")
    L += [f"    NUM_PARAMS_ = {nparams}", "};", f"#define NUM_PARAMS {nparams}",
          f"#define END_OF_SOUND_PARAMETERS {P['END_OF_SOUND_PARAMETERS']}", ""]
    L += ["enum { " + ", ".join(f"{k} = {v}" for k, v in DT.items() if k.startswith("DTYPE_") and "Force" not in k) + " };"]
    L += ["enum { " + ", ".join(f"{k} = {v}" for k, v in MENU.items()) + " };", ""]
    L += ["/* the value type of each parameter: DTYPE_* | MENU_* << 8 (menu.c parameter_dtypes) */",
          f"static const uint16_t PAR_DTYPE[{nparams}] = {{"]
    for i in range(0, nparams, 8):
        L.append("    " + ", ".join(f"{d | m << 8}" for d, m in dtypes[i:i + 8]) + ",")
    L += ["};", "", "/* names: TEXT_* -> short, category, long (menu.c valueNames) */",
          "enum { " + ", ".join(f"{k} = {v}" for k, v in T.items()) + " };",
          f"static const uint8_t TEXT_NAMES[{len(vnames)}][3] = {{"]
    for i in range(0, len(vnames), 6):
        L.append("    " + ", ".join(f"{{{a}, {b}, {c}}}" for a, b, c in vnames[i:i + 6]) + ",")
    L += ["};"]
    for nm, arr in (("SHORT_NAMES", shortn), ("CAT_NAMES", catn), ("LONG_NAMES", longn)):
        L.append(f"static const char *const {nm}[{len(arr)}] = {{")
        for i in range(0, len(arr), 8):
            L.append("    " + ", ".join('"' + s + '"' for s in arr[i:i + 8]) + ",")
        L.append("};")
    L += ["", "/* the edit pages (menuPages.h): [page][sub page] = 8 names (TEXT_*), 8 parameters; pages: VOICE1..7,",
          " * MENU_MIDI (global), LOAD, SAVE, PERF, SEQ, EUKLID, PATTERN_SETTINGS, RECORDING, FX_MODE */",
          f"#define LXR_NPAGES {len(pages)}",
          f"static const uint16_t LXR_PAGES[{len(pages)}][8][16] = {{"]
    for page in pages:
        rows = []
        for s in range(8):
            if s < len(page) and page[s]:
                t, p = sub(page[s])
            else:
                t, p = [0] * 8, [0] * 8
            rows.append("{" + ", ".join(map(str, t + p)) + "}")
        L.append("    {" + ",\n     ".join(rows) + "},")
    L += ["};", "", "/* modulation targets (Cc2Text.c): kits store a target as an index here {name TEXT_*, parameter} */",
          f"#define NUM_MOD_TARGETS {len(mods)}",
          f"static const uint16_t MOD_TARGETS[{len(mods)}][2] = {{"]
    for i in range(0, len(mods), 6):
        L.append("    " + ", ".join(f"{{{a}, {b}}}" for a, b in mods[i:i + 6]) + ",")
    L += ["};", "static const uint8_t MOD_TARGET_VOICE[6][2] = {" + ", ".join(f"{{{a}, {b}}}" for a, b in offs) + "};"]
    out.write_text("\n".join(L) + "\n")
    print(f"lxr params: {out}: {nparams} parameters ({P['END_OF_SOUND_PARAMETERS']} sound), {len(pages)} pages, "
          f"{len(mods)} mod targets")


if __name__ == "__main__":
    main()
