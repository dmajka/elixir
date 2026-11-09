#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: the manual's signal-flow diagrams (MANUAL.md section 10), as SVG files GitHub and the site both show:
#   docs/img/flow-track.svg     one track: source, filter, amp EG, shape, ..., volume and pan, then the four ways out
#   docs/img/flow-master.svg    the six tracks into the FX bus, the reverb and the mix, then the master's chain
# Follows voices.c (voices_render, voices_block) and engine.c (engine_block). Run it when the routing changes (and
# commit the SVGs):  tools/flow_svg.py
import html
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FONT = '-apple-system, BlinkMacSystemFont, "Segoe UI", "Helvetica Neue", Arial, sans-serif'
STYLE = f"""<style>
.bx {{ fill: #fcf8ef; stroke: #cdbb98; stroke-width: 1.2; }}
.bx.k {{ fill: #f3e6cb; }}
.md {{ fill: #f6f0e3; stroke: #cdbb98; stroke-width: 1.2; stroke-dasharray: 4 3; }}
.t {{ font: 700 13px {FONT}; fill: #2a241b; letter-spacing: .04em; }}
.d {{ font: 12px {FONT}; fill: #5e5649; }}
.p {{ font: 700 10px {FONT}; fill: #a85f00; letter-spacing: .06em; text-anchor: end; }}
.c {{ text-anchor: middle; }}
.a {{ stroke: #a85f00; stroke-width: 1.6; fill: none; }}
.a.m {{ stroke-dasharray: 3 3; stroke-width: 1.2; }}
.h {{ fill: #a85f00; }}
@media (prefers-color-scheme: dark) {{
  .bx {{ fill: #1a1713; stroke: #4a4135; }} .bx.k {{ fill: #2a2318; }} .md {{ fill: #15130f; stroke: #4a4135; }}
  .t {{ fill: #ece4d3; }} .d {{ fill: #b3aa99; }} .p {{ fill: #f5b04f; }} .a {{ stroke: #f5b04f; }} .h {{ fill: #f5b04f; }}
}}
</style>"""


def svg(w, h, label, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" role="img" '
            f'aria-label="{html.escape(label)}">\n<title>{html.escape(label)}</title>\n{STYLE}\n'
            '<defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
            'orient="auto-start-reverse"><path class="h" d="M0 0L10 5L0 10z"/></marker></defs>\n'
            + "\n".join(body) + "\n</svg>\n")


def box(x, y, w, h, title, lines=(), page="", cls="bx", centre=False):
    out = [f'<rect class="{cls}" x="{x}" y="{y}" width="{w}" height="{h}" rx="7"/>']
    tx, c = (x + w / 2, " c") if centre else (x + 12, "")
    out.append(f'<text class="t{c}" x="{tx}" y="{y + 20}">{html.escape(title)}</text>')
    for i, ln in enumerate(lines):
        out.append(f'<text class="d{c}" x="{tx}" y="{y + 37 + 15 * i}">{html.escape(ln)}</text>')
    if page:
        out.append(f'<text class="p" x="{x + w - 10}" y="{y + 20}">{page}</text>')
    return "".join(out)


def arrow(d, mod=False):
    return f'<path class="a{" m" if mod else ""}" d="{d}" marker-end="url(#ah)"/>'


def line(d):
    return f'<path class="a" d="{d}"/>'


def track():
    stages = [
        ("SOURCE", ["Oscillator, FM and transient, or a model:", "Rings, Plaits, MICRO or ENSEMBLE"], "EDIT"),
        ("FILTER", ["the LXR's types, LP4, combs, vowel, chord …;", "ACID, MS20, LADDER (FILTER X: env amt)"], "SCL"),
        ("AMP EG", ["Attack, decay, slope; GATE holds it for", "the note's length"], "ENV"),
        ("SHAPE", ["The waveshaper Drive goes into: LXR,", "FOLD, CLIP, FUZZ, TAPE, BITE …"], "SCL"),
        ("SAMPLE RATE · BITS · TILT", ["The rate reducer, the bitcrusher and", "a one-knob EQ"], "SCL"),
        ("INSERT FX", ["Phaser, flanger, chorus, tremolo,", "autopan, shifter or ring"], "SCL"),
        ("CROSS MOD · CHOKE", ["Another voice ducks it or rings it; a hit", "in its choke group cuts it short"], "LFO"),
        ("VOLUME · PAN", ["The mixer's level and place"], "GLO"),
    ]
    W, X, BW, H, PITCH, Y0 = 480, 20, 332, 58, 76, 16
    cx = X + BW / 2
    b = []
    for i, (t, ls, pg) in enumerate(stages):
        y = Y0 + PITCH * i
        b.append(box(X, y, BW, H, t, ls, pg))
        if i:
            b.append(arrow(f"M{cx} {y - PITCH + H} V{y - 1}"))
    top, bot = Y0, Y0 + PITCH * (len(stages) - 1) + H
    mods = ["LFO 1", "LFO 2", "Mod EG", "Velocity", "Random", "  per hit", "Step locks", "Global LFOs", "Morph",
            "", "move the", "knobs on", "the left"]
    mx = X + BW + 22
    b.append(f'<rect class="md" x="{mx}" y="{top}" width="{W - 20 - mx}" height="{bot - top}" rx="7"/>')
    b.append(f'<text class="t" x="{mx + 10}" y="{top + 20}">MOD</text>')
    for i, m in enumerate(mods):
        b.append(f'<text class="d" x="{mx + 10}" y="{top + 44 + 17 * i}">{html.escape(m)}</text>')
    for i in range(len(stages) - 1):
        y = Y0 + PITCH * i + H / 2
        b.append(arrow(f"M{mx - 2} {y} H{X + BW + 3}", mod=True))
    # the four ways out (1.0 Phase 16: the delay send)
    ys, yb, lw = bot + 18, bot + 38, 101
    lanes = [(20, "DRY", ["to the mix"]), (133, "FX SEND", ["to the FX bus,", "in place of dry"]),
             (246, "DLY SEND", ["to the delay,", "on top of dry"]), (359, "REV SEND", ["to the reverb,", "on top of dry"])]
    b.append(line(f"M{cx} {bot} V{ys}"))
    b.append(line(f"M{20 + lw // 2} {ys} H{359 + lw // 2}"))
    for x, t, ls in lanes:
        b.append(arrow(f"M{x + lw // 2} {ys} V{yb - 1}"))
        b.append(box(x, yb, lw, 66, t, ls, cls="bx k", centre=True))
    return svg(W, yb + 66 + 16, "Signal flow of one track: source, filter, amp EG, shape, sample rate, bits and tilt, "
               "insert FX, cross mod and choke, volume and pan, then dry, FX send, delay send and reverb send", b)


def master():
    W = 480
    b = [box(20, 16, 440, 44, "SIX VOICES", ["D1 D2 D3 SN CY HH, each as one track above"], cls="bx k",
             centre=True)]
    b.append('<text class="d" x="96" y="176">dry</text>')
    b.append(arrow("M88 60 V289"))
    b.append(arrow("M240 60 V83"))
    b.append(box(172, 84, 136, 58, "BUS DIRT", ["a drive before", "the effect"], "FX"))
    b.append(arrow("M240 142 V151"))
    b.append(box(172, 152, 136, 58, "FX BUS", ["RING, COMP, PHASER,", "FLANGER or SHIFT"], "FX"))
    b.append(arrow("M240 210 V219", mod=True))     # (1.0 Phase 16: as ROUTING sets it)
    b.append(box(172, 220, 136, 58, "DELAY", ["its sends, and the", "bus as ROUTING sets"], "FX"))
    b.append(arrow("M240 278 V289"))
    b.append(arrow("M392 60 V83"))
    b.append(box(324, 84, 136, 92, "REVERB", ["ROOM, SPRING,", "HALL or PLATE;", "Size, Damping,", "Level"], "FX"))
    b.append(arrow("M308 249 H360 V177", mod=True))
    b.append('<text class="d" x="366" y="214">ECHO</text><text class="d" x="366" y="229">&gt;REV</text>')
    b.append(arrow("M430 176 V289"))
    b.append(box(20, 290, 440, 44, "MIX", ["the dry voices, the bus, the echoes, the reverb's return"], cls="bx k", centre=True))
    chain = [("MASTER COMP", ["Thresh, Ratio, Attack, Release; sidechain:", "the mix, or one track to duck the rest"],
              "FX"),
             ("MASTER DIRT", ["TAPE, TUBE, FUZZ, DIODE or FOLD; Amount"], "FX"),
             ("MASTER FILTER", ["Low-pass one way, high-pass the other;", "Filter reso; type: LXR, RING, ACID or MS20"], "FX"),
             ("PERFORM FX", ["COMB, CUTS, CRUSH, REVERSE, STUTTER,", "then TAPE STOP"], "PERF"),
             ("MASTER", ["The MASTER knob: the volume"], "")]
    X, BW, H, PITCH, y = 74, 332, 58, 76, 356
    cx = X + BW / 2
    b.append(arrow(f"M{cx} 334 V{y - 1}"))
    for i, (t, ls, pg) in enumerate(chain):
        b.append(box(X, y, BW, H, t, ls, pg))
        b.append(arrow(f"M{cx} {y + H} V{y + PITCH - 1}"))
        y += PITCH
    b.append(box(X, y, BW, 44, "OUTPUT", ["the headphone and line outs"], cls="bx k", centre=True))
    return svg(W, y + 44 + 16, "Signal flow of the whole mix: the tracks' dry sound, the FX bus, the delay and the reverb into "
               "the mix, then the master compressor, master dirt, master filter, perform effects and the MASTER knob", b)


if __name__ == "__main__":
    out = ROOT / "docs/img"
    out.mkdir(parents=True, exist_ok=True)
    for name, s in (("flow-track.svg", track()), ("flow-master.svg", master())):
        (out / name).write_text(s, encoding="utf-8")
        print(f"flow: docs/img/{name} ({len(s)} B)")
