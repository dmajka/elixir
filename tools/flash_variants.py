#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Elixir: what a feature costs in flash, measured. Builds the firmware in a copy of the tree (build/flash_variants,
the working tree untouched) once as it is, then once for each variant below with that feature's code made
unreachable (its call or its case taken out, so -Os and --gc-sections drop it), and prints each saving in bytes.
The DSP and its tables only: a removed feature's names in menus, its page and its picture stay (usually 50-600 B
more). Each build takes ~12 s. See TECHNICAL_DOCUMENTATION.md section 11.1.
    tools/flash_variants.py              every variant
    tools/flash_variants.py ACID GRAIN   the variants whose names contain these
Needs what build.sh needs (the JieLi toolchain, Docker on macOS); PYTHON: a python3 with Pillow."""
import os, re, shutil, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FL = os.path.join(ROOT, "build", "flash_variants")
ORIG = os.path.join(ROOT, "firmware", "src")
SRC = os.path.join(FL, "firmware", "src")
PY = os.environ.get("PYTHON") or (os.path.join(ROOT, ".venv", "bin", "python")
                                  if os.path.exists(os.path.join(ROOT, ".venv", "bin", "python")) else "python3")
def ls(a, b): return ("dellines_all", a, b)
V = {
 "ACID filter (voice + DJ)": [
   ("dsp.c", "del", "case F_ACID: {                                   /* ck: P", "case F_MS20H: {                                  /* ck: G", "    case F_MS20L:\n"),
   ("dsp.c", "rep", "    case F_ACID:\n        ch_acid(s, buf, n, 0, 0);\n        break;\n", ""),
   ("dsp.c", "rep", "    case F_ACID:\n        ch_acid(s, buf, n, 1, w);\n        break;\n", "")],
 "MS20 LP/HP filters (voice + DJ)": [
   ("dsp.c", "del", "    case F_MS20H: {                                  /* ck: G", "default: {                                       /* LADDER. ck", ""),
   ("dsp.c", "rep", "    case F_MS20L:\n    case F_MS20H:\n        ch_ms20(s, buf, n, type == F_MS20H, 0, 0);\n        break;\n", ""),
   ("dsp.c", "rep", "        ch_ms20(s, buf, n, type == F_MS20H, 1, w);", "        dj_svf(s, buf, w, 0);")],
 "LADDER filter": [
   ("dsp.c", "del", "default: {                                       /* LADDER. ck", "static inline int32_t ms20_clip", "    default: break;\n    }\n}\n"),
   ("dsp.c", "del", "default: {                                       /* LADDER */", "static void ch_block", "    default: break;\n    }\n}\n")],
 "RING DJ filter (DSP)": [("perform.c", "rep", "        ring_block(l, r, d, w);", "        (void)w;")],
 "FX bus PHASER": [("fxbus.c", "rep", "    case FX_PHASER:\n        fx_phaser();\n        break;\n", "")],
 "FX bus FLANGER": [("fxbus.c", "rep", "    case FX_FLANGER:\n        fx_flanger();\n        break;\n", "")],
 "FX bus RING mod": [("fxbus.c", "rep", "    case FX_RING:\n        fx_ring();\n        break;\n", "")],
 "FX bus SHIFT": [("fxbus.c", "rep", "    case FX_SHIFT:\n        fx_shift();\n        break;\n", "")],
 "FX bus COMP": [("fxbus.c", "rep", "    case FX_COMP:\n        fx_comp();\n        break;\n", "")],
 "Delay GRAIN (DSP)": [("fxbus.c", "rep", "        if (is_grain())\n            fx_grain();\n        else\n            fx_tape();", "        fx_tape();")],
 "Delay TAPE modes (DSP)": [("fxbus.c", "rep", "        if (is_grain())\n            fx_grain();\n        else\n            fx_tape();", "        fx_grain();")],
 "Reverb PLATE": [("reverb.c", "rep", "        rev_plate(in_, out, n);", "        ;")],
 "Reverb HALL": [("reverb.c", "rep", "        rev_hall(in_, out, n);", "        ;")],
 "Reverb SPRING": [("reverb.c", "rep", "        rev_spring(in_, out, n);", "        ;")],
 "Reverb ROOM": [("reverb.c", "rep", "        rev_room(in_, out, n);", "        ;")],
 "Master compressor": [("engine.c", "rep", "    comp_master(mix_l, mix_r);", "    ;")],
 "Master DIRT": [("engine.c", "rep", "    dirt_master(mix_l, mix_r);", "    ;")],
 "STUTTER": [("perform.c", "rep", "    stutter(l, r);\n", "")],
 "PERFORM 2 COMB": [("perform.c", "del", "    a = pf_amount(on, PF_COMB, GD_COMB);", "    a = pf_amount(on, PF_CUTS, GD_CUTS);", "")],
 "PERFORM 2 CUTS": [("perform.c", "del", "    a = pf_amount(on, PF_CUTS, GD_CUTS);", "    a = pf_amount(on, PF_CRUSH, GD_CRUSH);", "")],
 "PERFORM 2 CRUSH": [("perform.c", "del", "    a = pf_amount(on, PF_CRUSH, GD_CRUSH);", "    st_was = pf.st_on;", "")],
 "Rings FM model": [("rings.c", "dl", "fm_run(m, f, s, b, d, p, in, acc);")],
 "Rings MODAL model": [("rings.c", "dl", "modal_run(m, in, acc, nm);"), ("rings.c", "dl", "nm = modal_coefs(")],
 "Rings STRING model": [("rings.c", "dl", "string_run(m, vi, f"), ("rings.c", "dl", "pluck_trigger(m, f"),
                        ("rings.c", "rep", " + pluck_next(m, vi, vint)", "")],
 "MICRO": [("voices.c", "repall", "mi_block(vi, v, buf);", ";"), ("voices.c", "repall", "mi_block((uint32_t)(v - vc), v, buf);", ";")],
 "ENSEMBLE": [("voices.c", "repall", "en_block(vi, v, buf);", ";")],
 "NOISE model": [("voices.c", "repall", "nz_block(vi, v, buf);", ";")],
 "USB audio (8 channels)": "ENV",
}
for e in ["BD", "SD", "HH", "PART", "STRING", "SWARM", "VA", "WSHP", "FM", "ADD", "CHORD", "SPEECH"]:
    V["Plaits P " + e] = [("plaits.c", "dl", "case PE_%s:" % e)]
V["Plaits P SPEECH (with its word banks)"] = [("plaits.c", "dl", "case PE_SPEECH:"), ("engine.c", "rep", "    pl_words_scan();   ", "    ;   ")]
V["All character filters (ACID, MS20, LADDER)"] = V["ACID filter (voice + DJ)"] + V["MS20 LP/HP filters (voice + DJ)"] + V["LADDER filter"] + [
   ("dsp.c", "rep", "static inline int flt_char(uint32_t t) { return t >= F_ACID && t <= F_LADDER; }",
    "static inline int flt_char(uint32_t t) { (void)t; return 0; }")]
V["Plaits (all twelve engines)"] = [("voices.c", "rep", "        pl_block(vi, v, buf);\n", "")]

def linestart(s, i): return s.rfind("\n", 0, i) + 1
def apply(edits):
    for e in edits:
        f = os.path.join(SRC, e[0]); s = open(f).read(); k = e[1]
        if k == "rep":
            assert s.count(e[2]) == 1, (e[0], e[2][:50], s.count(e[2])); s = s.replace(e[2], e[3])
        elif k == "repall":
            assert s.count(e[2]) >= 1, e; s = s.replace(e[2], e[3])
        elif k == "del":
            assert s.count(e[2]) == 1 and s.count(e[3]) == 1, (e[2][:40], s.count(e[2]), e[3][:40], s.count(e[3]))
            a = linestart(s, s.index(e[2])); b = linestart(s, s.index(e[3]))
            if e[2].startswith("    case F_MS20H"):     # (the MS20 block starts a line earlier: its case F_MS20L)
                a = linestart(s, a - 1)
            assert b > a, e; s = s[:a] + e[4] + s[b:]
        elif k == "dl":
            lines = s.split("\n"); n = [l for l in lines if e[2] not in l]; assert len(n) < len(lines), e; s = "\n".join(n)
        open(f, "w").write(s)
def build(env=None):
    r = subprocess.run(["./build.sh"], cwd=FL, env={**os.environ, "PYTHON": PY, **(env or {})}, capture_output=True, text=True)
    m = re.search(r"app\s+\S+\s+(\d+) B", r.stdout + r.stderr)
    if not m:
        return None, (r.stdout + r.stderr)[-1500:]
    return int(m.group(1)), ""
def reset():
    shutil.rmtree(SRC); shutil.copytree(ORIG, SRC)
if os.path.exists(FL):
    shutil.rmtree(FL)
shutil.copytree(ROOT, FL, ignore=shutil.ignore_patterns("build", ".git", ".venv", "web", "docs", "node_modules"))
reset(); base, err = build(); assert base, err
print(f"as it is: {base} B", flush=True)
out = {"_base": base}
only = sys.argv[1:]
for name, ed in V.items():
    if only and not any(o in name for o in only): continue
    reset()
    try:
        if ed == "ENV": sz, err = build({"ELIXIR_USB_AUDIO": "0"})
        else: apply(ed); sz, err = build()
    except AssertionError as x:
        sz, err = None, "edit: " + repr(x)
    out[name] = base - sz if sz else err
    print(f"{name}: {'-%d B' % out[name] if sz else 'FAILED ' + err[-600:]}", flush=True)
reset()
