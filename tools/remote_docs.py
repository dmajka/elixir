#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: the remote map's tables in the manual (MANUAL.md, section 15) and the cheat sheet (web/cheatsheet.html, page
# 6), written from the parameters tools/remote_params.py reads out of the firmware, so the numbers there can't drift
# from what Elixir does. Each table sits between two comments, which this rewrites (and nothing else):
#
#   <!-- remote:NAME -->  ...  <!-- /remote:NAME -->
#
# The words around them stay hand-written. web/make_site.py runs it on every site build and says when a file changed
# (commit it; tools/cheatsheet.sh reprints the cheat sheet's committed PDF; the site prints its own).
#
#   tools/remote_docs.py           rewrite the tables
#   tools/remote_docs.py --check   only say whether they are current (exit 1 if not)
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import remote_params  # noqa: E402

TRACKS = ["d1", "d2", "d3", "sn", "cy", "ch", "oh", "selected"]
HEAD = ["D1", "D2", "D3", "SN", "CY", "CH", "OH"]

# what a parameter is called in a table's cell: the manual's and the cheat sheet's own shorthand (a parameter missing
# here shows its name). The numbers never come from here.
CELL = {
    "global.tempo": "Tempo", "global.shuffle": "Shuffle", "global.morph": "Morph", "global.sample-rate": "SRate",
    "global.master-filter": "Master filter", "global.roll-rate": "Roll rate", "global.repeat": "Repeat",
    "global.tape-time": "Tape time", "global.fx-wet": "FX wet", "global.fx-p1": "P1", "global.fx-p2": "P2",
    "global.fx-p3": "P3", "global.fx-p4": "P4", "global.decay": "Decay", "global.tape-speed": "Speed", "global.bus-dirt": "Bus dirt", "global.echo-to-reverb": "Echo > rev",
    "global.saturate": "Saturate", "global.reverb-level": "Rev level", "global.reverb-size": "Rev size",
    "global.reverb-damping": "Rev damp", "global.master-dirt": "Master dirt", "global.density": "DENSITY",
    "global.ghost-velocity": "Ghost vel", "global.build": "Build", "global.vary": "Vary",
    "global.drill": "DRILL", "global.jitter": "JITTER", "global.cuts": "CUTS", "global.pump": "PUMP",
    "global.comb-note": "COMB note", "global.comb-feedback": "COMB fb", "global.crush": "CRUSH",
    "global.reverse-length": "REV length", "global.filter-resonance": "Filter reso",
    "global.delay.time": "Time", "global.delay.feedback": "Feedback", "global.delay.tone": "Tone",
    "global.delay.band": "Band", "global.delay.wow": "Wow", "global.delay.glitch": "Glitch",
    "global.delay.route": "Route", "global.delay.reverb-to-fx": "Rev>FX",
    "global.ring.wet": "Wet", "global.ring.slope": "Slope", "global.ring.tone": "Tone", "global.ring.tune": "Tune",
    "global.ring.spread": "Spread", "global.lfo-4-depth": "LFO 4 depth",
    "global.play-stop": "PLAY / STOP", "global.record": "REC", "global.fill": "FILL", "global.repeat-held": "REPEAT",
    "global.freeze": "FREEZE", "global.throw-reverb": "THROW REV", "global.throw-fx": "THROW FX",
    "global.feedback-push": "FB PUSH", "global.tape-stop": "TAPE STOP", "global.low-pass-sweep": "LP sweep",
    "global.high-pass-sweep": "HP sweep", "global.reverse": "REVERSE", "global.pattern-previous": "◀ pattern",
    "global.pattern-next": "pattern ▶", "global.control-all": "CONTROL ALL (tap or hold)",
    "global.control-all-revert": "CONTROL ALL revert", "global.density-all-in": "all in",
}
for i in range(1, 5):
    CELL[f"global.macro-{i}"] = f"Macro {i}"
    CELL[f"global.lfo-{i}-depth"] = CELL.get(f"global.lfo-{i}-depth", f"LFO {i} depth")
for t, h in zip(TRACKS, HEAD + ["selected"]):
    for k in ("select", "roll", "drill", "density-exclude"):
        CELL[f"global.{k}.{t}"] = h
for k in ("half", "jitter", "cuts", "short", "pump", "comb", "space", "crush"):
    CELL[f"global.effect.{k}"] = k.upper()
NOTE = {"tracks.ch.decay": "closed", "tracks.oh.decay": "open"}   # (the manual's: CH and OH decays apart)
# each document's own words, over CELL's
MANUAL_CELL = {"global.fx-wet": "FX dry/wet", "global.pattern-previous": "previous", "global.pattern-next": "next",
               "global.delay.time": "Delay Time (GRAIN: Position)", "global.delay.feedback": "Delay Feedback",
               "global.delay.tone": "Delay Tone", "global.delay.band": "Delay Band (0 OFF)",
               "global.delay.wow": "Delay Wow (GRAIN: Size)", "global.delay.glitch": "Delay Glitch (GRAIN: Density)",
               "global.delay.route": "Route (the middle PARALLEL, left DLY > FX, right FX > DLY)",
               "global.delay.reverb-to-fx": "Rev>FX", "global.ring.wet": "RING Wet", "global.ring.slope": "RING Slope",
               "global.ring.tone": "RING Tone", "global.ring.tune": "RING Tune (the chord)",
               "global.ring.spread": "RING Spread", "global.lfo-4-depth": "LFO 4's depth"}
MANUAL_ROW = {"pitch": "Pitch (coarse)", "delay-send": "Delay send (1.0)", "density": "DENSITY (1.0)"}
CHEAT_CELL = {"global.echo-to-reverb": "Echo>rev", "global.master-dirt": "Mstr dirt", "global.reverse-length": "REV len",
              "global.lfo-1-depth": "LFO1 dep", "global.lfo-2-depth": "LFO2 dep", "global.lfo-3-depth": "LFO3 dep",
              "global.lfo-4-depth": "LFO4 dep", "global.play-stop": "PLAY/STOP",
              "global.control-all": "CTRL ALL tap/hold", "global.control-all-revert": "CA revert"}
for k in ("roll", "drill"):
    CHEAT_CELL[f"global.{k}.selected"] = "sel"
CHEAT_ROW = {"Transport, held": "Transport · held", "Effects, tap or hold": "Effects: tap latches, hold holds",
             "DENSITY out": "DENSITY out ⇄"}
# a row of GLOBAL's buttons is named by its first button (so the name follows the buttons if they move)
BUTTON_ROW = {"global.play-stop": "Transport, held", "global.tape-stop": "Held · patterns", "global.select.d1": "Select",
              "global.roll.d1": "Roll (held)", "global.drill.d1": "DRILL (held)",
              "global.effect.half": "Effects, tap or hold", "global.density-exclude.d1": "DENSITY out"}


def cell(p, words=None):
    return (words or {}).get(p["id"]) or CELL.get(p["id"], p["name"])


class Map:
    """the parameters, looked up as the tables need them"""

    def __init__(self, doc):
        self.p = doc["parameters"]
        self.by_cc = {}
        for p in self.p:
            if p["messageType"] == "cc" and p.get("channelGroup"):
                self.by_cc[(p["channelGroup"], p["number"])] = p

    def tracks_rows(self):                           # [(row name, [8 parameters])], by CC
        rows = {}
        for p in self.p:
            if p["scope"] == "remote-tracks" and p.get("track"):
                key = p["id"].split(".", 2)[2]
                rows.setdefault(key, [None] * 8)[TRACKS.index(p["track"])] = p
        out = sorted(rows.values(), key=lambda r: r[0]["number"])
        for r in out:
            remote_params.need(all(r), f"TRACKS row {r[0]['id']}: not eight columns")
        return [(r[0]["name"].removeprefix("D1 "), r) for r in out]

    def global_row(self, first):                     # GLOBAL's CCs first .. first + 7 (None: no such CC)
        return [self.by_cc.get(("remoteGlobal", first + c)) for c in range(8)]

    def global_knobs(self):
        return [p for p in self.p if p["scope"] == "remote-global" and p["messageType"] == "cc"
                and p["controlType"] == "continuous" and not p["id"].startswith("global.roll-fader.")
                and p["id"] != "global.master-volume"]

    def grid(self):                                  # the knob grid: rows of 8 whose CCs are knobs, 8 .. 47 (1.4: or
        knobs = {p["number"] for p in self.global_knobs()}   # free, 19 and 20: six knobs a row at least)
        used = {n for (g, n) in self.by_cc if g == "remoteGlobal"}
        rows = [f for f in range(0, 128, 8) if all(f + c in knobs or f + c not in used for c in range(8))
                and sum(f + c in knobs for c in range(8)) >= 6]
        remote_params.need(rows, "GLOBAL has no row of eight knobs")
        k = 1                                        # (the first block; the delay's row, 96 .., is off the grid)
        while k < len(rows) and rows[k] == rows[k - 1] + 8:
            k += 1
        return rows[:k]

    def off_grid(self):
        grid = {r + c for r in self.grid() for c in range(8)}
        knobs = [p for p in self.global_knobs() if p["number"] not in grid]
        delay = [p for p in knobs if p["id"].startswith("global.delay.")]
        rest = sorted((p for p in knobs if p not in delay), key=lambda p: (p["category"] != "Ring", p["number"]))
        return sorted(delay, key=lambda p: p["number"]), rest

    def button_rows(self):                           # GLOBAL's rows of 8 holding buttons
        firsts = {p["number"] // 8 * 8 for p in self.p if p["scope"] == "remote-global" and p["controlType"] == "button"}
        return [(f, self.global_row(f)) for f in range(min(firsts), max(firsts) + 8, 8)]   # (knob rows between too)

    def rolls(self):
        return sorted((p for p in self.p if p["id"].startswith("global.roll-fader.")), key=lambda p: p["number"])

    def range_of(self, ps):
        ns = [p["number"] for p in ps]
        return f"{min(ns)}–{max(ns)}"


def row_name(row):
    first = next((p for p in row if p), None)
    return BUTTON_ROW.get(first["id"], first["category"]) if first else ""


# ---- the manual (Markdown: <!-- grid --> tables, as make_site's markdown() draws them) ----
def md_row(cells):
    return "|" + "|".join(f" {c} " if c else " " for c in cells) + "|"


SEP = "|---|---|---|---|---|---|---|---|---|"


def manual_blocks(m):
    out = {}
    rows = m.tracks_rows()
    knobs = [p for _, r in rows for p in r if p["controlType"] == "continuous"]
    buttons = [p for _, r in rows for p in r if p["controlType"] == "button"]
    first = rows[0][1][0]["number"]
    lines = [f"**TRACKS (channel 15): knobs CC {m.range_of(knobs)}, buttons {m.range_of(buttons)}.** The number is "
             f"{first} + 8 × row + column − 1.", "", "<!-- grid -->", md_row([""] + HEAD + ["Selected"]),
             SEP]
    for name, r in rows:
        key = r[0]["id"].split(".", 2)[2]
        lines.append(md_row([MANUAL_ROW.get(key, name)] + [f"**{p['number']}**" + (f" {NOTE[p['id']]}" if p["id"] in NOTE else "")
                                      for p in r]))
    out["manual-tracks"] = lines
    grid = m.grid()
    lines = [f"**GLOBAL (channel 16): knobs and faders, CC {grid[0]}–{grid[-1] + 7}**", "", "<!-- grid -->",
             md_row([""] + [str(c) for c in range(1, 9)]), SEP]
    for f in grid:
        lines.append(md_row([f"{f}–{f + 7}"] + [f"**{p['number']}** {cell(p, MANUAL_CELL)}" if p else f"**{f + c}** –"
                                                for c, p in enumerate(m.global_row(f))]))
    out["manual-global-knobs"] = lines
    delay, rest = m.off_grid()
    lines = [md_row(["CC", "Sets", "", "CC", "Sets"]), "|---|---|---|---|---|"]
    for i in range(max(len(delay), len(rest))):
        a = delay[i] if i < len(delay) else None
        b = rest[i] if i < len(rest) else None
        lines.append(md_row([f"**{a['number']}**" if a else "", cell(a, MANUAL_CELL) if a else "", "",
                             f"**{b['number']}**" if b else "", cell(b, MANUAL_CELL) if b else ""]))
    out["manual-global-off-grid"] = lines
    brows = m.button_rows()
    nums = [p["number"] for _, r in brows for p in r if p and p["controlType"] == "button"]
    lines = [f"**GLOBAL (channel 16): buttons, CC or note {min(nums)}–{max(nums)}**", "", "<!-- grid -->",
             md_row([""] + [str(c) for c in range(1, 9)]), SEP]
    for f, r in brows:
        knob_row = all(p and p["controlType"] != "button" for p in r)
        name = "(knobs: the " + (r[0]["category"].lower() if r[0] else "") + ")" if knob_row else row_name(r)
        lines.append(md_row([name] + [
            "" if not p else f"**{p['number']}** {cell(p, MANUAL_CELL if not knob_row else None)}"
            if p["controlType"] == "button" or knob_row else f"**{p['number']}** (a knob: {cell(p, MANUAL_CELL)})"
            for p in r]))
    out["manual-global-buttons"] = lines
    rolls = m.rolls()
    out["manual-rolls"] = [f"**GLOBAL (channel 16): a roll per track, CC {m.range_of(rolls)}** (from 1.0; a fader each)",
                           "", "<!-- grid -->", md_row([""] + HEAD + ["Selected"]), SEP,
                           md_row(["Roll (by value)"] + [f"**{p['number']}**" for p in rolls])]
    return out


# ---- the cheat sheet (HTML, its page 6's tables) ----
def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def cheat_blocks(m):
    out = {}
    rows = m.tracks_rows()
    lines = ['<table class="grid">', "  <tr><th></th>" + "".join(f"<th>{h}</th>" for h in HEAD) + "<th>sel</th></tr>"]
    for name, r in rows:
        tog = " ⇄" if r[0]["controlType"] == "button" else ""
        short = {"Filter frequency": "Filter freq", "Density": "DENSITY"}.get(name, name)
        lines.append(f"  <tr><td>{esc(short)}{tog}</td>" + "".join(f"<td>{p['number']}</td>" for p in r) + "</tr>")
    out["cheat-tracks"] = lines + ["</table>"]

    def cells(r):
        return "".join("<td></td>" if not p else f"<td><b>{p['number']}</b>{esc(cell(p, CHEAT_CELL))}</td>" for p in r)

    lines = ['<table class="grid cells">']
    for f in m.grid():
        lines.append(f"  <tr><td>{f}–{f + 7}</td>{cells(m.global_row(f))}</tr>")
    out["cheat-global-knobs"] = lines + ["</table>"]
    delay, rest = m.off_grid()
    pad = lambda ps: ps + [None] * (8 - len(ps))  # noqa: E731
    out["cheat-global-off-grid"] = ['<table class="grid cells">', f"  <tr><td>Delay</td>{cells(pad(delay))}</tr>",
                                    f"  <tr><td>RING · LFO 4</td>{cells(pad(rest))}</tr>", "</table>"]
    lines = ['<table class="grid cells">']
    for f, r in m.button_rows():
        if all(p and p["controlType"] != "button" for p in r):
            continue                                 # (the delay's knobs: the table above)
        name = row_name(r)
        lines.append(f"  <tr><td>{esc(CHEAT_ROW.get(name, name))}</td>" + "".join(
            "<td></td>" if not p else f"<td><b>{p['number']}</b>{esc(cell(p, CHEAT_CELL))}</td>" if p["controlType"] == "button"
            else f"<td><b>{p['number']}</b><i>knob</i></td>" for p in r) + "</tr>")
    rolls = m.rolls()
    lines.append("  <tr><td>Roll (fader)</td>" + "".join(f"<td><b>{p['number']}</b>{esc(cell({**p, 'id': p['id'].replace('roll-fader', 'roll')}, CHEAT_CELL))}</td>"
                                                         for p in rolls) + "</tr>")
    out["cheat-global-buttons"] = lines + ["</table>"]
    return out


def rewrite(text, blocks, file, indent=""):
    for name, lines in blocks.items():
        pat = re.compile(rf"(<!-- remote:{name} -->\n)(.*?)(^[ \t]*<!-- /remote:{name} -->)", re.S | re.M)
        if len(pat.findall(text)) != 1:
            raise SystemExit(f"remote_docs: {file}: <!-- remote:{name} --> .. <!-- /remote:{name} --> not found once")
        body = "".join(indent + ln + "\n" if ln else "\n" for ln in lines)
        text = pat.sub(lambda mt: mt[1] + body + mt[3], text)
    return text


TARGETS = [("MANUAL.md", manual_blocks, ""), ("web/cheatsheet.html", cheat_blocks, "    ")]


def update(doc=None, check=False):
    """rewrite (or check) the tables; returns the files that were (or would be) changed"""
    m = Map(doc or remote_params.build())
    changed = []
    for rel, blocks, indent in TARGETS:
        path = ROOT / rel
        old = path.read_text(encoding="utf-8")
        new = rewrite(old, blocks(m), rel, indent)
        if new != old:
            changed.append(rel)
            if not check:
                path.write_text(new, encoding="utf-8")
    return changed


if __name__ == "__main__":
    check = sys.argv[1:] == ["--check"]
    changed = update(check=check)
    if changed:
        print(f"remote_docs: {', '.join(changed)} {'out of date' if check else 'rewritten'} from the firmware's remote map")
    else:
        print("remote_docs: the manual's and the cheat sheet's remote tables are current")
    sys.exit(1 if check and changed else 0)
