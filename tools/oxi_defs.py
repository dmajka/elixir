#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: instrument definitions for the OXI One (mk1 and mk2), written from the remote map (tools/remote_params.py's
# document) on every site build, beside the Hapax's (tools/hapax_defs.py):
#
#   ELIXIR_TRACKS.oxiindef   the remote's TRACKS channel (15): per track volume, pan, filter .. mute and solo, as
#                            modulation and CC targets with 4-letter names (D1Vo, SNFl, SLDc: SL the selected track)
#   ELIXIR_GLOBAL.oxiindef   the remote's GLOBAL channel (16): tempo, morph, the effects, delay, reverb, macros, rolls
#
# The format (JSON; no schema published): as the OXI App imports and MIDI Guide exports it (midi.guide/blog/
# adding-oxi-instrument-definition-support): {id, name, abbr, manufacturer, parameters: [{type "cc", name, abbr (4
# characters), minimum, maximum, default_value, nr1 (the CC), nr2 0}], script null}. A definition names parameters
# only: the channel and the drum notes are set on the OXI. CCs past 119 (MIDI's channel mode numbers) are left out.
#
#   tools/oxi_defs.py [OUT_DIR]          (default: build/oxi)
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import remote_params  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
TRACKS_FILE, GLOBAL_FILE = "ELIXIR_TRACKS.oxiindef", "ELIXIR_GLOBAL.oxiindef"
TRACK_TAG = {"d1": "D1", "d2": "D2", "d3": "D3", "sn": "SN", "cy": "CY", "ch": "CH", "oh": "OH", "selected": "SL"}
TRACK_PARAM = {                                      # tracks.<track>.<key>: two letters after the track's two
    "volume": "Vo", "pan": "Pn", "filter-frequency": "Fl", "resonance": "Rs", "decay": "Dc", "pitch": "Pt",
    "delay-send": "Dl", "fx-send": "Fx", "reverb-send": "Rv", "sample-rate": "SR", "lfo-amount": "Lf",
    "density": "Dn", "mute": "Mu", "solo": "So"}
CENTERED = {"pan", "density"}                        # (64 is their middle: the default)
GLOBAL_ABBR = {                                      # global.<key>: what words alone can't make readable in four
    "lfo-1-depth": "LFO1", "lfo-2-depth": "LFO2", "lfo-3-depth": "LFO3", "lfo-4-depth": "LFO4",
    "ring.wet": "RgWt", "ring.slope": "RgSl", "ring.tone": "RgTn", "ring.tune": "RgTu", "ring.spread": "RgSp",
    "tempo": "Tmpo", "shuffle": "Shuf", "morph": "Mrph", "sample-rate": "SRat", "master-filter": "MFlt",
    "filter-resonance": "MRes", "roll-rate": "RlRt", "repeat": "Rept", "tape-time": "TpTm", "fx-wet": "FxWt",
    "fx-p1": "FxP1", "fx-p2": "FxP2", "fx-p3": "FxP3", "fx-p4": "FxP4", "bus-dirt": "BDrt", "echo-to-reverb": "EcRv",
    "saturate": "Satu", "reverb-level": "RvLv", "reverb-size": "RvSz", "reverb-damping": "RvDp",
    "master-dirt": "MDrt", "density": "Dens", "ghost-velocity": "GhVl", "build": "Bild", "vary": "Vary",
    "macro-1": "Mac1", "macro-2": "Mac2", "macro-3": "Mac3", "macro-4": "Mac4", "drill": "Drll", "jitter": "Jitr",
    "cuts": "Cuts", "pump": "Pump", "comb-note": "CbNt", "comb-feedback": "CbFb", "crush": "Crsh",
    "reverse-length": "RvsL", "play-stop": "Play", "record": "Rec", "fill": "Fill", "repeat-held": "RptH",
    "freeze": "Frez", "throw-reverb": "ThRv", "throw-fx": "ThFx", "feedback-push": "FbPs", "tape-stop": "TpSt",
    "low-pass-sweep": "LPSw", "high-pass-sweep": "HPSw", "reverse": "Revs", "pattern-previous": "PtPr",
    "pattern-next": "PtNx", "control-all": "CAll", "control-all-revert": "CARv", "density-all-in": "DnAl",
    "effect.half": "Half", "effect.jitter": "JitB", "effect.cuts": "CutB", "effect.short": "Shrt",
    "effect.pump": "PmpB", "effect.comb": "Comb", "effect.space": "Spce", "effect.crush": "CrsB",
    "delay.time": "DlTm", "delay.feedback": "DlFb", "delay.tone": "DlTn", "delay.band": "DlBd", "delay.wow": "DlWw",
    "delay.glitch": "DlGl", "delay.route": "DlRt", "delay.reverb-to-fx": "DlRF"}
GLOBAL_TRACKED = {"select": "S", "roll": "R", "drill": "D", "density-exclude": "X", "roll-fader": "F"}  # + track


def need(cond, msg):
    if not cond:
        raise SystemExit(f"oxi_defs: {msg}")


def abbr_of(p):
    pid = p["id"]
    if m := re.fullmatch(r"tracks\.(\w+)\.([\w-]+)", pid):
        t, k = m.groups()
        need(t in TRACK_TAG and k in TRACK_PARAM, f"{pid}: no OXI abbreviation (TRACK_TAG, TRACK_PARAM)")
        return TRACK_TAG[t] + TRACK_PARAM[k]
    key = pid.removeprefix("global.")
    if m := re.fullmatch(r"([\w-]+)\.(d1|d2|d3|sn|cy|ch|oh|selected)", key):
        need(m[1] in GLOBAL_TRACKED, f"{pid}: no OXI abbreviation (GLOBAL_TRACKED)")
        return GLOBAL_TRACKED[m[1]] + TRACK_TAG[m[2]]          # (RD1: D1's roll, FSL: the selected track's fader)
    need(key in GLOBAL_ABBR, f"{pid}: no OXI abbreviation (GLOBAL_ABBR)")
    return GLOBAL_ABBR[key]


def definition(doc, group, ident, title, abbr):
    params = sorted((p for p in doc["parameters"] if p.get("channelGroup") == group and p["messageType"] == "cc"
                     and p["number"] <= 119), key=lambda p: p["number"])
    need(params, f"no CCs on {group}")
    out = []
    for p in params:
        a = abbr_of(p)
        need(re.fullmatch(r"[A-Za-z0-9-]{1,4}", a), f"{p['id']}: abbreviation {a!r}: 1-4 letters, digits or -")
        key = p["id"].rsplit(".", 1)[-1]
        out.append({"type": "cc", "name": p["name"].replace("Selected track", "Selected"), "abbr": a,
                    "minimum": p["minimum"], "maximum": p["maximum"],
                    "default_value": 64 if p["id"].startswith("tracks.") and key in CENTERED else 0,
                    "nr1": p["number"], "nr2": 0})
    abbrs = [p["abbr"] for p in out]
    need(len(set(abbrs)) == len(abbrs), f"{title}: an abbreviation used twice: "
         f"{sorted(a for a in set(abbrs) if abbrs.count(a) > 1)}")
    return {"id": ident, "name": title, "abbr": abbr, "manufacturer": "Elixir", "parameters": out, "script": None}


def build(doc):
    return {TRACKS_FILE: definition(doc, "remoteTracks", "elixir_fm1_tracks", "Elixir Tracks", "ElxT"),
            GLOBAL_FILE: definition(doc, "remoteGlobal", "elixir_fm1_global", "Elixir Global", "ElxG")}


def write(doc, out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    files = build(doc)
    for f, d in files.items():
        (out_dir / f).write_text(json.dumps(d, indent=2) + "\n", encoding="utf-8", newline="\n")
    return [out_dir / f for f in files]


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) > 1:
        sys.exit("usage: tools/oxi_defs.py [OUT_DIR]")
    for p in write(remote_params.build(), a[0] if a else ROOT / "build/oxi"):
        print(f"oxi_defs: {p}")
