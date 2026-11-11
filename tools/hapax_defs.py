#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: instrument definitions for Squarp's Hapax (1.3.2), written from the remote map (tools/remote_params.py's
# document, the JSON the controllers page is built from) on every site build, so a CC the firmware moves moves here:
#
#   ELIXIR_DRUMS.txt    a DRUM track: seven lanes play D1 .. OH on their own channels (Elixir's track channels, 1-7
#                       by default) at note 60 (the voice's own pitch); the track's own channel (a drum track's is for
#                       its automation alone) is the remote's TRACKS (15): its CC names, pots and program changes
#                       (patterns 1-32) go there
#   ELIXIR_GLOBAL.txt   a DRUM track on the remote's GLOBAL channel (16): its lanes are the performance buttons that act
#                       while held (a note's length is the hold), its CCs the effects, tempo, morph, macros ..
#
# The format: https://squarp.net/hapax/instr_def/template.txt (VERSION 1). Names and the comment take letters,
# digits, ' ', '_', '-' and '+' only; CCs past 119 (MIDI's channel mode numbers) are left out.
#
#   tools/hapax_defs.py [OUT_DIR]          (default: build/hapax; reads the firmware through remote_params.build())
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import remote_params  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
DRUMS, GLOBAL = "ELIXIR_DRUMS.txt", "ELIXIR_GLOBAL.txt"
NOTE = 60                                             # dsp.c SEQ_DEFAULT_NOTE: a voice at its own pitch (note ANY)
TRACK_CHANNELS = {"d1": 1, "d2": 2, "d3": 3, "sn": 4, "cy": 5, "ch": 6, "oh": 7}   # CONFIG: MIDI CHANNELS' defaults
DRUM_POTS = ["volume", "pan", "filter-frequency", "resonance", "decay", "pitch", "delay-send", "reverb-send"]
GLOBAL_POTS = ["morph", "master-filter", "filter-resonance", "fx-wet", "reverb-level", "density", "macro-1", "macro-2"]
OK = re.compile(r"[A-Za-z0-9 _+-]*")


def need(cond, msg):
    if not cond:
        raise SystemExit(f"hapax_defs: {msg}")


def name(text):
    """a Hapax name: the selected track as SEL, the rest of the characters it can't show as spaces"""
    text = text.replace("Selected track", "SEL").replace("/", "-")
    text = re.sub(r"[^A-Za-z0-9 _+-]", " ", text)
    text = re.sub(r" +", " ", text).strip()
    need(text and OK.fullmatch(text), f"no Hapax name for {text!r}")
    return text


def comment(lines):
    out = [re.sub(r" +", " ", re.sub(r"[^A-Za-z0-9 _+-]", " ", ln)).strip() for ln in lines]
    for ln in out:
        need(OK.fullmatch(ln), f"comment line {ln!r}")
    return out


def by_id(params):
    return {p["id"]: p for p in params}


def ccs(params, group):
    return sorted((p for p in params if p.get("channelGroup") == group and p["messageType"] == "cc"
                   and p["number"] <= 119), key=lambda p: p["number"])


def patterns(params, group):
    pcs = sorted((p for p in params if p.get("channelGroup") == group and p["messageType"] == "program-change"),
                 key=lambda p: p["number"])
    need(pcs, f"no program changes on {group}")
    return [f"{p['number'] + 1} {name(p['name'])}" for p in pcs]          # (the Hapax counts PC from 1)


def pots(ids, params):
    out = []
    for i, k in enumerate(ids, 1):
        p = params.get(k)
        need(p and p["messageType"] == "cc" and p["controlType"] == "continuous" and p["number"] <= 119,
             f"pot {i}: {k} is no continuous CC on the remote")
        out.append(f"{i} CC:{p['number']}{' ' * (8 - len(str(p['number'])))}# {name(p['name'])}")
    return out


def document(head, lanes, pcs, cc_rows, pot_rows, notes):
    need(len(lanes) <= 16, f"{len(lanes)} drum lanes: the Hapax has 16")
    need(len(pcs) <= 128, "more than 128 PC names")
    need(len(pot_rows) <= 8, "more than 8 pots")
    names = [r.split(" ", 1)[1] for r in cc_rows]
    need(len(set(r.split(" ", 1)[0] for r in cc_rows)) == len(cc_rows), "a CC named twice")
    need(all(OK.fullmatch(n) for n in names), "a CC name the Hapax can't show")
    return "\n".join([
        *head,
        "[DRUMLANES]", *lanes, "[/DRUMLANES]", "",
        "# Program changes: Elixir launches patterns 1-32 (queued, as its pattern page does)",
        "[PC]", *pcs, "[/PC]", "",
        "[CC]", *cc_rows, "[/CC]", "",
        "[ASSIGN]", *pot_rows, "[/ASSIGN]", "",
        "[AUTOMATION]", "[/AUTOMATION]", "",
        "[COMMENT]", *comment(notes), "[/COMMENT]", ""])


def head(doc, title, settings, about):
    return [f"# Elixir for the FM-1: {title}",
            f"# Written from Elixir {doc['firmwareVersion']}'s remote map ({doc['sourceRevision']}) by tools/hapax_defs.py:",
            "# https://dmajka.github.io/elixir/controllers/ (the map: https://dmajka.github.io/elixir/data/)",
            *(f"# {ln}" for ln in about),
            "# Copy it to the HAPAX folder of the SD card; load it on a track: 2ND + the track's button, INSTRUMENT DEFINITION.",
            "", "VERSION 1", *settings, ""]


def build(doc):
    params = doc["parameters"]
    ids = by_id(params)
    tr, gl = remote_params.GROUPS["remote-tracks"], remote_params.GROUPS["remote-global"]
    tch, gch = 15, 16                                # (the remote's defaults: CONFIG: REMOTE)

    # ---- the drums: lanes on the track channels, the rest on TRACKS ----
    lanes = []
    for row, (t, ch) in enumerate(TRACK_CHANNELS.items(), 1):
        label = remote_params.LABELS[remote_params.TRACKS.index(t)]
        lanes.append(f"{row}:NULL:{ch}:{NOTE} {label}")
    drums = document(
        head(doc, "a drum track", ["TRACKNAME ELIXIR", "TYPE DRUM", "OUTPORT NULL           # the port the FM-1 is on: set it on the track",
                                   f"OUTCHAN {tch}             # the remote's TRACKS channel (CONFIG: REMOTE): the CCs, pots and PCs below",
                                   "INPORT NULL", "INCHAN NULL", "MAXRATE NULL"],
             ["Lanes 1-7 play D1 D2 D3 SN CY CH OH on Elixir's track channels (CONFIG: MIDI CHANNELS, 1-7 by default)",
              f"at note {NOTE}, the voice's own pitch (with the track's MIDI NOTE at ANY, the default). The track's own",
              f"channel is the remote's TRACKS ({tch}): per-track volume, pan, filter, decay, sends, mutes ..",
              "Pots 1-8: the selected track's volume, pan, filter, resonance, decay, pitch, delay and reverb sends."]),
        lanes, patterns(params, tr),
        [f"{p['number']} {name(p['name'])}" for p in ccs(params, tr)],
        pots([f"tracks.selected.{k}" for k in DRUM_POTS], ids),
        ["Elixir drums", "Lanes 1-7 D1 to OH on channels 1-7 note 60",
         f"Track channel {tch} the remote TRACKS map", "Pots the selected track", "PC 1-32 patterns 1-32"])

    # ---- the global channel: the held performance buttons as lanes, the knobs as CCs ----
    held = [p for p in ccs(params, gl) if p["controlType"] == "button" and p.get("buttonBehavior") == "held"
            and not p["id"].startswith(("global.roll.", "global.drill."))]
    need(held, "no held buttons on GLOBAL")
    glanes = [f"{row}:NULL:NULL:{p['number']} {name(p['name'])}" for row, p in enumerate(held, 1)]
    glob = document(
        head(doc, "the global channel", ["TRACKNAME ELIXIR GLOBAL", "TYPE DRUM", "OUTPORT NULL           # the port the FM-1 is on: set it on the track",
                                        f"OUTCHAN {gch}             # the remote's GLOBAL channel (CONFIG: REMOTE)",
                                        "INPORT NULL", "INCHAN NULL", "MAXRATE NULL"],
             [f"A track on the remote's GLOBAL channel ({gch}). Its lanes are the performance buttons that act while held:",
              "a note's length is how long it holds (FILL, FREEZE, TAPE STOP ..). The CCs: tempo, morph, the effects,",
              "the reverb, the macros, the rolls; the tap-to-latch effects (HALF .. CRUSH) are left to a controller.",
              "Pots 1-8: morph, master filter, its resonance, FX wet, reverb level, density, macros 1 and 2."]),
        glanes, patterns(params, gl),
        [f"{p['number']} {name(p['name'])}" for p in ccs(params, gl)],
        pots([f"global.{k}" for k in GLOBAL_POTS], ids),
        ["Elixir global", f"Channel {gch} the remote GLOBAL map", "Lanes the held performance buttons",
         "A note holds for its length", "PC 1-32 patterns 1-32"])
    return {DRUMS: drums, GLOBAL: glob}


def write(doc, out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    files = build(doc)
    for f, text in files.items():
        need(text.isascii(), f"{f}: not ASCII")
        (out_dir / f).write_text(text, encoding="utf-8", newline="\n")
    return [out_dir / f for f in files]


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) > 1:
        sys.exit("usage: tools/hapax_defs.py [OUT_DIR]")
    for p in write(remote_params.build(), a[0] if a else ROOT / "build/hapax"):
        print(f"hapax_defs: {p}")
