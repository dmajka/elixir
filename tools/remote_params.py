#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: the remote map as data (1.3.2): every CC, note and program change the remote's two channels take, read from
# the firmware that acts on them, for the controller mapper (web/controllers) and anyone else; and (1.3.2) the notes
# that play each track on its own channel (a drum pad's hit, at the pad's velocity):
#
#   elixir-midi-parameters.json        {schemaVersion, firmwareVersion, sourceRevision, parameters: [...]}, sorted by id
#   elixir-midi-parameters.meta.json   the same version and revision, the JSON's SHA-256, the parameter count
#   elixir-midi-parameters.csv         the same records as a table (written from the JSON's, never kept apart)
#
# The numbers come from the firmware alone: remote.c's layout (RM_CC0, RM_TB0, RM_GB0, RM_ROLL0, RM_XK) and tables
# (the TRACKS rows' enum, RM_GK, the GLOBAL rows' enum, RM_PF, RM_FX), midi.c's CCs on both channels (AK_CC_*), seq.c's
# NUM_PATTERN, kit.c's NUM_KITS and the factory bank's size (tools/gen_factory_kits.py), engine.c's track channels
# and dsp.c's SEQ_DEFAULT_NOTE (a track's hit). What a number is called and
# what it does (META below) is keyed by the firmware's own symbol (PAR_BPM, PF_FILL, RR_DECAY ..), so a number that
# moves in the firmware moves here, and a symbol the firmware gains that META doesn't name stops the build.
# web/make_site.py runs it on every site build; the mapper's build (web/controllers/vite.config.ts) does too.
#
#   tools/remote_params.py [OUT_DIR]          (default: build/params)
#   tools/remote_params.py --src DIR [OUT_DIR] (another firmware/src: the tests' changed copy)
import csv
import hashlib
import io
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = 1
NAME = "elixir-midi-parameters"
TRACKS = ["d1", "d2", "d3", "sn", "cy", "ch", "oh", "selected"]
LABELS = ["D1", "D2", "D3", "SN", "CY", "CH", "OH", "Selected track"]
SCOPES = {"remote-tracks", "remote-global", "track-voices", "system"}
GROUPS = {"remote-tracks": "remoteTracks", "remote-global": "remoteGlobal"}
# (track-voices: each track on its own channel, CONFIG: MIDI CHANNELS; the group by the track)
TRACK_GROUPS = {t: "track" + t.upper() for t in TRACKS[:7]}
TYPES = {"cc", "note", "program-change", "realtime"}
CONTROLS = {"continuous", "button", "selection", "realtime"}
BUTTONS = {"press-or-state", "held", "press", "tap-latch-hold"}
FEEDBACK = {"source": "USB", "controllerCompatibility": "unverified"}
KNOB_MODES = ["PICKUP", "ABS", "REL"]
PRESS = "CC above 0 or note on presses; CC 0 or note off releases."
TAP = ("A tap (under {tap} ms) latches, a press again turns it off, a longer press holds. With CONFIG: REMOTE "
       "Mute/solo at TOGGLE the button's state is the latch.")

# ---- what each firmware symbol is: (id key, name, category[, interpretation]) ----
TRACK_KNOBS = {                                       # remote.c's TRACKS knob rows (enum RR_*)
    "RR_VOL": ("volume", "Volume", "Mixer"), "RR_PAN": ("pan", "Pan", "Mixer"),
    "RR_FILTER": ("filter-frequency", "Filter frequency", "Sound"), "RR_RESO": ("resonance", "Resonance", "Sound"),
    "RR_DECAY": ("decay", "Decay", "Sound"), "RR_PITCH": ("pitch", "Pitch", "Sound"),
    "RR_DLYS": ("delay-send", "Delay send", "Sends"), "RR_FXS": ("fx-send", "FX send", "Sends"),
    "RR_REVS": ("reverb-send", "Reverb send", "Sends"), "RR_SRATE": ("sample-rate", "Sample rate", "Sound"),
    "RR_RUSH": ("rush", "Rush", "Performance",
                "PERFORM: RUSH on this track, 0 off to 100% (a roll that takes the kit over). One track rushes at a "
                "time: the last turned up; back to 0 on the rushing track lets it go. Recorded under REC as RUSH locks."),
    "RR_RNDP": ("density", "Density", "Sequencer",
                "The track's own density, centered at 64, on top of PERFORM's (not saved)."),
}
TRACK_BUTTONS = {"RB_MUTE": ("mute", "Mute", "Mixer"), "RB_SOLO": ("solo", "Solo", "Mixer")}
GLOBAL_KNOBS = {                                      # remote.c RM_GK's entries
    "PAR_BPM": ("tempo", "Tempo", "Performance"), "PAR_SHUFFLE": ("shuffle", "Shuffle", "Performance"),
    "PAR_MORPH": ("morph", "Morph", "Performance"),
    "PAR_VOICE_DECIMATION_ALL": ("sample-rate", "Sample rate", "Sound"),
    "VP_DJ": ("master-filter", "Master filter", "Performance"), "PAR_ROLL": ("roll-rate", "Roll rate", "Performance"),
    "VP_REPEAT": ("repeat", "Repeat", "Performance"), "VP_TAPE": ("tape-time", "Tape time", "Delay"),
    "PAR_FX_DRY_WET": ("fx-wet", "FX wet", "Effects"), "PAR_FX_P1": ("fx-p1", "FX P1", "Effects"),
    "PAR_FX_P2": ("fx-p2", "FX P2", "Effects"), "PAR_FX_P3": ("fx-p3", "FX P3", "Effects"),
    "PAR_FX_P4": ("fx-p4", "FX P4", "Effects"),
    "VP_WDEC": ("decay", "Global decay", "Performance",
                "PERFORM 2: WEAR's Decay: every voice's decay, 64 the kit as it is, 0 a click, 127 four times as long."),
    "VP_WSPD": ("tape-speed", "Tape speed", "Performance",
                "PERFORM 2: WEAR's Speed: the whole kit's pitch, 64 as it is, 0 and 127 an octave down and up."), "PAR_FXDIRT": ("bus-dirt", "Bus dirt", "Effects"),
    "PAR_ECHOREV": ("echo-to-reverb", "Echo to reverb", "Routing"),
    "PAR_DSAT": ("saturate", "Saturate / Grain scatter", "Effects"),
    "PAR_REV_LEVEL": ("reverb-level", "Reverb level", "Reverb"), "PAR_REV_SIZE": ("reverb-size", "Reverb size", "Reverb"),
    "PAR_REV_DAMP": ("reverb-damping", "Reverb damping", "Reverb"),
    "PAR_MDIRT_AMT": ("master-dirt", "Master dirt", "Effects"),
    "VP_DENS": ("density", "Global density", "Sequencer"), "VP_GHOST": ("ghost-velocity", "Ghost velocity", "Sequencer"),
    "VP_BUILD": ("build", "Build", "Performance"), "VP_DVARY": ("vary", "Vary", "Performance"),
    "VP_M1": ("macro-1", "Macro 1", "Macros"), "VP_M1 + 1": ("macro-2", "Macro 2", "Macros"),
    "VP_M1 + 2": ("macro-3", "Macro 3", "Macros"), "VP_M1 + 3": ("macro-4", "Macro 4", "Macros"),
    "VP_P2": ("drill", "Drill", "Performance"), "VP_P2 + 1": ("jitter", "Jitter", "Performance"),
    "VP_P2 + 2": ("cuts", "Cuts", "Performance"), "VP_P2 + 3": ("pump", "Pump", "Performance"),
    "VP_P2 + 4": ("comb-note", "Comb note", "Effects"), "VP_P2 + 5": ("comb-feedback", "Comb feedback", "Effects"),
    "VP_P2 + 6": ("crush", "Crush", "Effects"), "VP_P2 + 7": ("reverse-length", "Reverse length", "Effects"),
    "GLV(0, 1)": ("lfo-1-depth", "LFO 1 depth", "Modulation"), "GLV(1, 1)": ("lfo-2-depth", "LFO 2 depth", "Modulation"),
    "GLV(2, 1)": ("lfo-3-depth", "LFO 3 depth", "Modulation"), "GLV(3, 1)": ("lfo-4-depth", "LFO 4 depth", "Modulation"),
    "VP_DJRESO": ("filter-resonance", "Filter resonance / Ring decay", "Sound",
                  "The master filter's resonance; with the filter's type RING, its Decay."),
    "VP_DJWET": ("ring.wet", "Ring Wet", "Ring"), "VP_DJSLOPE": ("ring.slope", "Ring Slope", "Ring"),
    "VP_DJTONE": ("ring.tone", "Ring Tone", "Ring"), "VP_DJTUNE": ("ring.tune", "Ring Tune", "Ring"),
    "VP_DJSPREAD": ("ring.spread", "Ring Spread", "Ring"),
    "PAR_DLY_TIME": ("delay.time", "Delay Time / Grain position", "Delay"),
    "PAR_DLY_FB": ("delay.feedback", "Delay Feedback", "Delay"), "PAR_DLY_TONE": ("delay.tone", "Delay Tone", "Delay"),
    "PAR_DLY_BAND": ("delay.band", "Delay Band", "Delay", "0 turns Band off; 1–127 set it."),
    "PAR_DLY_WOW": ("delay.wow", "Delay Wow / Grain size", "Delay"),
    "PAR_GLITCH": ("delay.glitch", "Delay Glitch / Grain density", "Delay"),
    "PAR_DLY_ROUTE": ("delay.route", "Delay Route", "Delay"), "PAR_REV_FX": ("delay.reverb-to-fx", "Delay Reverb to FX", "Delay"),
}
PERF_BUTTONS = {                                      # remote.c RM_PF's entries: (.., behavior, interpretation)
    "RM_PLAY": ("play-stop", "Play / Stop", "Transport", "press", "A press starts the pattern or song (as Play says), or stops it."),
    "RM_REC": ("record", "Record", "Transport", "press", "A press turns recording on or off."),
    "PF_FILL": ("fill", "Fill", "Transport", "held", "Fill while held."),
    "PF_STUT": ("repeat-held", "Repeat (held)", "Transport", "held", "Repeat while held."),
    "PF_FREEZE": ("freeze", "Freeze", "Transport", "held", "Freeze the delay while held."),
    "PF_THROW_R": ("throw-reverb", "Throw reverb", "Transport", "held", "Throw to the reverb while held."),
    "PF_THROW_F": ("throw-fx", "Throw FX", "Transport", "held", "Throw to the FX bus while held."),
    "PF_PUSH": ("feedback-push", "Feedback push", "Transport", "held", "Push the delay's feedback while held."),
    "PF_TAPE": ("tape-stop", "Tape stop", "Performance", "held", "Held performance action."),
    "PF_SWEEP_LP": ("low-pass-sweep", "Low-pass sweep", "Performance", "held", "Held performance action."),
    "PF_SWEEP_HP": ("high-pass-sweep", "High-pass sweep", "Performance", "held", "Held performance action."),
    "PF_REVERSE": ("reverse", "Reverse", "Performance", "held", "Held performance action."),
    "RM_PREV": ("pattern-previous", "Previous pattern", "Patterns", "press", "Queue the previous pattern."),
    "RM_NEXT": ("pattern-next", "Next pattern", "Patterns", "press", "Queue the next pattern."),
    "RM_CAH": ("control-all", "Control All", "Performance", "tap-latch-hold",
               TAP + " While on, a track's knobs, voice CCs and NRPNs move every track in Control All; off keeps the edits."),
}
FX_BUTTONS = {"PF_HALF": ("half", "Half"), "PF_JITTER": ("jitter", "Jitter"), "PF_CUTS": ("cuts", "Cuts"),
              "PF_SHORT": ("short", "Short"), "PF_PUMP": ("pump", "Pump"), "PF_COMB": ("comb", "Comb"),
              "PF_SPACE": ("space", "Space"), "PF_CRUSH": ("crush", "Crush")}   # remote.c RM_FX's entries
GLOBAL_ROWS = {"RG_PERF", "RG_PERF2", "RG_SELECT", "RG_ROLL", "RG_DRILL", "RG_HOLD", "RG_LATCH", "RG_DENS"}


class SourceError(SystemExit):
    pass


def need(cond, msg):
    if not cond:
        raise SourceError(f"remote_params: {msg}")


def define(src, name, file):
    m = re.search(rf"^#define {name} (?:\(\s*)?(0x[0-9A-Fa-f]+|\d+)u?\b", src, re.M)
    need(m, f"{file}: #define {name} not found")
    return int(m[1], 0)


def table(src, decl, file):
    """the entries of `decl = { .. };` as their source text, whitespace normalized"""
    m = re.search(re.escape(decl) + r"\s*=\s*\{(.*?)\};", src, re.S)
    need(m, f"{file}: {decl} not found")
    body = re.sub(r"/\*.*?\*/", "", m[1], flags=re.S)
    out, depth, cur = [], 0, ""
    for ch in body:                                   # (split on commas outside parentheses: GLV(0, 1))
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    return [re.sub(r"\s+", " ", e).strip() for e in out if e.strip()]


def enum_with(src, first, file):
    """the enum whose first member is `first`"""
    m = re.search(r"enum\s*\{\s*(" + re.escape(first) + r"\b[^}]*)\}", src)
    need(m, f"{file}: enum {{ {first}, .. }} not found")
    return [e.strip() for e in m[1].split(",") if e.strip()]


def factory_kits():
    with tempfile.TemporaryDirectory() as d:
        h = Path(d) / "factory_kits.h"
        subprocess.run([sys.executable, str(ROOT / "tools/gen_factory_kits.py"), str(h),
                        str(ROOT / "assets/kits/factory")], check=True, capture_output=True)
        return define(h.read_text(), "FACTORY_NKITS", "gen_factory_kits.py")


def revision(src_dir):
    r = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=ROOT, capture_output=True, text=True)
    rev = r.stdout.strip() if r.returncode == 0 else "unknown"
    if src_dir.resolve() != (ROOT / "firmware/src").resolve():
        return rev + "+changed-copy"
    d = subprocess.run(["git", "status", "--porcelain", "--", "firmware/src"], cwd=ROOT, capture_output=True, text=True)
    return rev + ("-dirty" if d.stdout.strip() else "")


def build(src_dir=ROOT / "firmware/src", kits=None):
    """the parameters, from the firmware in src_dir"""
    files = {f: (src_dir / f).read_text(encoding="utf-8") for f in ("remote.c", "midi.c", "seq.c", "kit.c", "elixir.c",
                                                                      "engine.c", "dsp.c")}
    rc, mc = files["remote.c"], files["midi.c"]
    cc0, ntk, tb0, ntb = (define(rc, n, "remote.c") for n in ("RM_CC0", "RM_NTK", "RM_TB0", "RM_NTB"))
    gb0, ngb, roll0, nxk = (define(rc, n, "remote.c") for n in ("RM_GB0", "RM_NGB", "RM_ROLL0", "RM_NXK"))
    tap = define(rc, "RM_TAP_MS", "remote.c")
    xk = [int(x, 0) for x in table(rc, f"static const uint8_t RM_XK[RM_NXK]", "remote.c")]
    gk = table(rc, "static const uint16_t RM_GK[RM_NGK]", "remote.c")
    pf = table(rc, "static const uint8_t RM_PF[16]", "remote.c")
    fx = table(rc, "static const uint8_t RM_FX[8]", "remote.c")
    rr, rb, rg = (enum_with(rc, f, "remote.c") for f in ("RR_VOL", "RB_MUTE", "RG_PERF"))
    need(len(xk) == nxk, f"remote.c: RM_XK has {len(xk)} entries, RM_NXK says {nxk}")
    need(len(gk) == 40 + nxk, f"remote.c: RM_GK has {len(gk)} entries, not 40 + RM_NXK")
    need(len(rr) * 8 == ntk, f"remote.c: {len(rr)} TRACKS knob rows (enum RR_*) for RM_NTK {ntk}")
    need(len(rb) * 8 == ntb, f"remote.c: {len(rb)} TRACKS button rows (enum RB_*) for RM_NTB {ntb}")
    need(len(rg) * 8 == ngb, f"remote.c: {len(rg)} GLOBAL button rows (enum RG_*) for RM_NGB {ngb}")
    tempo = re.search(r"if \(p == PAR_BPM\) \{\s*\*lo = (\d+);\s*\*hi = (\d+);", rc)
    need(tempo, "remote.c rm_range: the tempo's range not found")
    vol, morph, slot, factory = (define(mc, n, "midi.c") for n in ("AK_CC_VOL", "AK_CC_MORPH", "AK_CC_SLOT",
                                                                   "AK_CC_FACTORY"))
    npat = define(files["seq.c"], "NUM_PATTERN", "seq.c")
    free = define(files["seq.c"], "CROLL_FREE", "seq.c")
    nkits = define(files["kit.c"], "NUM_KITS", "kit.c")
    nfactory = kits if kits is not None else factory_kits()
    hit = define(files["dsp.c"], "SEQ_DEFAULT_NOTE", "dsp.c")
    tch = track_channels(files["engine.c"])

    out = []
    knob = {"messageType": "cc", "alternateMessageTypes": [], "minimum": 0, "maximum": 127,
            "controlType": "continuous", "encoderBehavior": KNOB_MODES, "feedback": FEEDBACK}
    button = {"messageType": "cc", "alternateMessageTypes": ["note"], "minimum": 0, "maximum": 127,
              "controlType": "button", "feedback": FEEDBACK}

    def doc(file, symbol):
        return {"source": f"firmware/src/{file}", "symbol": symbol}

    def add(**p):
        p.setdefault("shortName", p["name"])
        out.append(p)

    def known(meta, sym, where):
        need(sym in meta, f"remote.c: {where} is {sym!r}, which tools/remote_params.py doesn't name (add it to META)")
        return meta[sym]

    # TRACKS: knobs CC RM_CC0 + 8 x row + column, buttons RM_TB0 + 8 x row + column; column 8 the selected track
    for r, sym in enumerate(rr):
        key, name, cat, *interp = known(TRACK_KNOBS, sym, f"TRACKS knob row {r}")
        for c, t in enumerate(TRACKS):
            add(id=f"tracks.{t}.{key}", name=f"{LABELS[c]} {name}", category=cat, scope="remote-tracks", track=t,
                channelGroup="remoteTracks", number=cc0 + 8 * r + c, **knob,
                interpretation=interp[0] if interp else f"The track's {name.lower()}, over its whole range, as the "
                "panel's knob sets it. Column 8 follows the selected track; CH and OH share the hat voice, all but "
                "its decay.",
                documentation=doc("remote.c", f"{sym} (RM_CC0 + 8 x row + column)"))
    for r, sym in enumerate(rb):
        key, name, cat = known(TRACK_BUTTONS, sym, f"TRACKS button row {r}")
        for c, t in enumerate(TRACKS):
            add(id=f"tracks.{t}.{key}", name=f"{LABELS[c]} {name}", category=cat, scope="remote-tracks", track=t,
                channelGroup="remoteTracks", number=tb0 + 8 * r + c, **button, buttonBehavior="press-or-state",
                interpretation=f"{PRESS} CONFIG: REMOTE Mute/solo PRESS: a press flips it; TOGGLE: the button's "
                "state is it.", documentation=doc("remote.c", f"{sym} (RM_TB0 + 8 x row + column)"))

    # GLOBAL knobs: RM_GK's first 40 at CC RM_CC0 + i, the rest at RM_XK's CCs
    gcc = {}
    for i, sym in enumerate(gk):
        if sym == "VP_NONE":                          # (a CC left free: 1.4's 19, 20)
            continue
        key, name, cat, *interp = known(GLOBAL_KNOBS, sym, f"RM_GK[{i}]")
        cc = cc0 + i if i < 40 else xk[i - 40]
        gcc[sym] = cc
        if sym == "PAR_BPM":
            interp = [f"0–127 spans {tempo[1]}–{tempo[2]} BPM (relative encoders go further)."]
        add(id=f"global.{key}", name=name, category=cat, scope="remote-global", channelGroup="remoteGlobal",
            number=cc, **knob, interpretation=interp[0] if interp else f"{name}, over its whole range, as the panel sets it.",
            documentation=doc("remote.c", f"RM_GK[{i}] {sym}" + (f", RM_XK[{i - 40}]" if i >= 40 else "")))
    for p in out:                                     # (RING's notes name the CCs its root and decay share)
        if p["category"] == "Ring":
            p["interpretation"] = (f"With the master filter's type RING: its root is the master filter (CC "
                                   f"{gcc['VP_DJ']}), its decay Filter resonance (CC {gcc['VP_DJRESO']}).")

    # GLOBAL buttons: RM_GB0 + 8 x row + column; a CC that is a knob (RM_XK) is a knob only (rm_in, rm_nobtn)
    for r, row in enumerate(rg):
        need(row in GLOBAL_ROWS, f"remote.c: GLOBAL button row {row} unknown to tools/remote_params.py")
        for c in range(8):
            cc, t = gb0 + 8 * r + c, TRACKS[c]
            sym = f"{row}[{c}]"
            if cc in xk:
                continue
            if row in ("RG_PERF", "RG_PERF2"):
                f = pf[(r - rg.index("RG_PERF")) * 8 + c]
                if f == "RM_NONE":
                    continue
                key, name, cat, beh, interp = known(PERF_BUTTONS, f, f"RM_PF[{(r - rg.index('RG_PERF')) * 8 + c}]")
                interp = interp.format(tap=tap)
                sym, track = f"RM_PF {f}", None
            elif row == "RG_SELECT":
                key, name, cat, beh, track = ((f"select.{t}", f"Select {LABELS[c]}", "Track selection", "press", t)
                                              if c < 7 else ("control-all-revert", "Control All revert", "Performance",
                                                             "press", None))
                interp = ("Select the track; while Control All is on, leave it out or put it back." if c < 7 else
                          "Revert the kit to before the last Control All (until a kit loads).")
            elif row == "RG_ROLL":
                key, name, cat, beh, track = f"roll.{t}", f"{LABELS[c]} roll (held)", "Roll", "held", t
                interp = "Roll the track while held (column 8: the track selected at the press)."
            elif row == "RG_DRILL":
                key, name, cat, beh, track = f"drill.{t}", f"{LABELS[c]} drill (held)", "Performance", "held", t
                interp = "Drill the track while held."
            elif row == "RG_HOLD":
                k, name = known(FX_BUTTONS, fx[c], f"RM_FX[{c}]")
                key, cat, beh, track, sym = f"effect.{k}", "Performance buttons", "tap-latch-hold", None, f"RM_FX {fx[c]}"
                interp = TAP.format(tap=tap)
            elif row == "RG_DENS":
                key, name, cat, beh, track = ((f"density-exclude.{t}", f"Density exclude {LABELS[c]}", "Sequencer",
                                               "press", t) if c < 7 else
                                              ("density-all-in", "Density all in", "Sequencer", "press", None))
                interp = ("Leave the track out of DENSITY; again, put it back." if c < 7 else
                          "Put every track back in DENSITY.")
            else:
                need(False, f"remote.c: GLOBAL button {cc} (row {row}) is neither a button nor a knob in RM_XK")
            add(id=f"global.{key}", name=name, category=cat, scope="remote-global", channelGroup="remoteGlobal",
                **({"track": track} if track else {}), number=cc, **button, buttonBehavior=beh,
                interpretation=f"{interp} {PRESS}", documentation=doc("remote.c", f"{sym} (RM_GB0 + 8 x row + column)"))

    # GLOBAL: a track's roll by a CC's value
    for c, t in enumerate(TRACKS):
        add(id=f"global.roll-fader.{t}", name=f"{LABELS[c]} roll rate", category="Roll", scope="remote-global",
            channelGroup="remoteGlobal", track=t, number=roll0 + c, **knob,
            interpretation=f"0 off; 1–{free - 1} on the grid (1/4 to 1/32); {free}–127 free (1/32 to 1/256). "
            "Several tracks may roll at once; column 8 rolls the track selected as it left 0.",
            documentation=doc("remote.c", "RM_ROLL0 + column (seq.c CROLL_FREE)"))

    # both channels: the kits, the master volume, the patterns (midi.c midi_message, midi_kit_cc)
    for prefix, scope in (("tracks", "remote-tracks"), ("global", "remote-global")):
        common = {"scope": scope, "channelGroup": GROUPS[scope], "alternateMessageTypes": []}
        for n in range(npat):
            add(id=f"{prefix}.pattern.{n + 1}", name=f"Pattern {n + 1}", category="Patterns", **common,
                messageType="program-change", number=n, minimum=0, maximum=npat - 1, controlType="selection",
                interpretation="The pattern changes at the pattern's end; at once if stopped.",
                documentation=doc("midi.c", "program change: seq_set_next_pattern (NUM_PATTERN, seq.c)"))
        add(id=f"{prefix}.master-volume", name="Master volume", category="Mixer", **common, messageType="cc",
            number=vol, minimum=0, maximum=127, controlType="continuous",
            interpretation="127 the most; kept in Elixir's settings.", documentation=doc("midi.c", "AK_CC_VOL"))
        for sym, cc, key, name, top, what in (
                ("AK_CC_MORPH", morph, "morph-kit", "Morph kit", nkits - 1, "sets LOAD > MORPH's target"),
                ("AK_CC_SLOT", slot, "slot-kit", "Slot kit", nkits - 1, "loads at once"),
                ("AK_CC_FACTORY", factory, "factory-kit", "Factory kit", nfactory - 1, "loads at once")):
            add(id=f"{prefix}.{key}", name=name, category="Kits", **common, messageType="cc", number=cc, minimum=0,
                maximum=top, controlType="selection",
                interpretation=f"The kit's number from 0 (shown from 1); it {what}.", documentation=doc("midi.c", sym))

    # each track on its own channel (midi.c midi_message, midi_note_on): a note on plays it at its velocity, recorded
    # under REC while playing; the default note at the voice's own pitch while the track's MIDI NOTE is ANY
    for c, t in enumerate(TRACKS[:7]):
        add(id=f"tracks.{t}.hit", name=f"{LABELS[c]} Hit", category="Pads", scope="track-voices", track=t,
            channelGroup=TRACK_GROUPS[t], messageType="note", alternateMessageTypes=[], number=hit, minimum=1,
            maximum=127, controlType="button", buttonBehavior="press",
            interpretation=f"Plays {LABELS[c]} at the pad's velocity, at the voice's own pitch, on the track's own channel "
            f"(CONFIG: MIDI CHANNELS, {tch[c]} by default). Under REC, while playing, it records at that velocity. "
            "Needs the track's MIDI NOTE at ANY (the default); a note off does nothing.",
            documentation=doc("midi.c", "midi_note_on (dsp.c SEQ_DEFAULT_NOTE, engine.c PAR_MIDI_CHAN_*)"))

    for key, name, status in (("clock", "MIDI clock", 0xF8), ("start", "MIDI start", 0xFA), ("stop", "MIDI stop", 0xFC)):
        add(id=f"system.{key}", name=name, category="MIDI sync", scope="system", messageType="realtime",
            alternateMessageTypes=[], number=status, minimum=0, maximum=0, controlType="realtime",
            interpretation="MIDI real-time, on no channel: 24 clocks a quarter note, followed with Elixir's tempo at "
            "MIDI. Not a controller custom mode's CC.", documentation=doc("midi.c", "midi_message (status >= 0xF8)"))

    version = re.search(r'#define ELIXIR_VERSION "([^"]+)"', files["elixir.c"])
    need(version, "elixir.c: ELIXIR_VERSION not found")
    params = sorted(out, key=lambda p: p["id"])
    validate(params)
    return {"schemaVersion": SCHEMA, "firmwareVersion": version[1], "sourceRevision": revision(src_dir),
            "parameters": params}


def track_channels(src):
    """engine.c engine_globals_default: tracks 1..6 on channels 1..6, the seventh's (1-based)"""
    m = re.search(r"for \(t = 0; t < 6u; t\+\+\)\s*par\[PAR_MIDI_CHAN_1 \+ t\] = \(uint8_t\)\(t \+ (\d+)u\);\s*"
                  r"par\[PAR_MIDI_CHAN_7\] = (\d+);", src)
    need(m, "engine.c: the track channels' defaults (PAR_MIDI_CHAN_*) not found")
    return [t + int(m[1]) for t in range(6)] + [int(m[2])]


# the records' contract (web/controllers/src/elixir/registry-schema.ts checks the same in the mapper's build)
FIELDS = ["id", "name", "shortName", "category", "scope", "track", "channelGroup", "messageType",
          "alternateMessageTypes", "number", "minimum", "maximum", "controlType", "interpretation", "buttonBehavior",
          "encoderBehavior", "feedback", "documentation"]
REQUIRED = {"id", "name", "shortName", "category", "scope", "messageType", "alternateMessageTypes", "number",
            "minimum", "maximum", "controlType", "interpretation", "documentation"}
ALIASES = set()                                       # (intended shared addresses: none yet)


def validate(params):
    ids = set()
    for p in params:
        pid = p.get("id")
        need(isinstance(pid, str) and re.fullmatch(r"[a-z0-9]+(?:[.-][a-z0-9]+)*", pid), f"bad id {pid!r}")
        need(pid not in ids, f"{pid}: id twice")
        ids.add(pid)
        need(set(p) <= set(FIELDS), f"{pid}: unknown fields {set(p) - set(FIELDS)}")
        need(REQUIRED <= set(p), f"{pid}: missing {REQUIRED - set(p)}")
        for f in ("name", "shortName", "category", "interpretation"):
            need(isinstance(p[f], str) and p[f].strip(), f"{pid}: {f} empty")
        for f in ("number", "minimum", "maximum"):
            need(type(p[f]) is int, f"{pid}: {f} not an integer")
        need(p["scope"] in SCOPES and p["messageType"] in TYPES and p["controlType"] in CONTROLS, f"{pid}: bad enum")
        group = TRACK_GROUPS.get(p.get("track")) if p["scope"] == "track-voices" else GROUPS.get(p["scope"])
        need(p.get("channelGroup") == group, f"{pid}: channelGroup doesn't match scope")
        need("track" not in p or p["track"] in TRACKS, f"{pid}: bad track")
        need(isinstance(p["alternateMessageTypes"], list) and set(p["alternateMessageTypes"]) <= TYPES,
             f"{pid}: bad alternateMessageTypes")
        need(0 <= p["minimum"] <= p["maximum"] <= 127, f"{pid}: bad range")
        top = 255 if p["messageType"] == "realtime" else 127
        need(0 <= p["number"] <= top, f"{pid}: number out of range")
        need(p["messageType"] != "realtime" or 0xF8 <= p["number"] <= 0xFF, f"{pid}: not a real-time status")
        need(p["messageType"] != "program-change" or p["minimum"] <= p["number"] <= p["maximum"],
             f"{pid}: program outside its range")
        need((p["controlType"] == "button") == ("buttonBehavior" in p), f"{pid}: buttonBehavior iff a button")
        need("buttonBehavior" not in p or p["buttonBehavior"] in BUTTONS, f"{pid}: bad buttonBehavior")
        need("encoderBehavior" not in p or p["encoderBehavior"] == KNOB_MODES, f"{pid}: bad encoderBehavior")
        need("feedback" not in p or p["feedback"] == FEEDBACK, f"{pid}: bad feedback")
        need(isinstance(p["documentation"], dict) and p["documentation"].get("source", "").startswith("firmware/src/"),
             f"{pid}: documentation must name its firmware source")
    seen = {}                                         # one address (channel group, type, number), one parameter
    for p in params:
        for t in [p["messageType"], *p["alternateMessageTypes"]]:
            a = (p.get("channelGroup"), t, p["number"])
            need(a not in seen or (seen[a], p["id"]) in ALIASES,
                 f"{p['id']} and {seen.get(a)}: the same address {a}")
            seen[a] = p["id"]


CSV_COLUMNS = ["id", "name", "category", "scope", "channelGroup", "defaultChannel", "messageType",
               "alternateMessageTypes", "number", "minimum", "maximum", "controlType", "buttonBehavior", "track",
               "interpretation", "source", "symbol"]
DEFAULT_CHANNEL = {"remoteTracks": 15, "remoteGlobal": 16,   # (CONFIG: REMOTE's defaults; the FM-1 can change them;
                   **{g: c + 1 for c, g in enumerate(TRACK_GROUPS.values())}}   # .. MIDI CHANNELS' the tracks')


def to_csv(doc):
    out = io.StringIO()
    w = csv.writer(out, lineterminator="\n")
    w.writerow(CSV_COLUMNS)
    scopes, types = ["remote-tracks", "remote-global", "track-voices", "system"], ["cc", "note", "program-change", "realtime"]
    for p in sorted(doc["parameters"], key=lambda p: (scopes.index(p["scope"]), types.index(p["messageType"]),
                                                      p["number"])):   # (a table: by channel and number)
        row = {**p, "defaultChannel": DEFAULT_CHANNEL.get(p.get("channelGroup"), ""),
               "alternateMessageTypes": " ".join(p["alternateMessageTypes"]),
               "source": p["documentation"]["source"], "symbol": p["documentation"]["symbol"]}
        w.writerow([row.get(c, "") for c in CSV_COLUMNS])
    return out.getvalue()


def write(doc, out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    data = (json.dumps(doc, ensure_ascii=False, indent=1) + "\n").encode("utf-8")
    meta = {"schemaVersion": SCHEMA, "file": f"{NAME}.json", "firmwareVersion": doc["firmwareVersion"],
            "sourceRevision": doc["sourceRevision"], "sha256": hashlib.sha256(data).hexdigest(),
            "parameterCount": len(doc["parameters"]), "csv": f"{NAME}.csv",
            "sources": ["firmware/src/remote.c", "firmware/src/midi.c", "firmware/src/seq.c", "firmware/src/kit.c",
                        "firmware/src/engine.c", "firmware/src/dsp.c", "assets/kits/factory", "tools/remote_params.py"]}
    (out_dir / f"{NAME}.json").write_bytes(data)
    (out_dir / f"{NAME}.csv").write_text(to_csv(json.loads(data)), encoding="utf-8")
    (out_dir / f"{NAME}.meta.json").write_text(json.dumps(meta, indent=1) + "\n", encoding="utf-8")
    return out_dir / f"{NAME}.json"


if __name__ == "__main__":
    a, src = sys.argv[1:], ROOT / "firmware/src"
    if a[:1] == ["--src"]:
        src, a = Path(a[1]), a[2:]
    if len(a) > 1:
        sys.exit("usage: tools/remote_params.py [--src DIR] [OUT_DIR]")
    doc = build(src)
    path = write(doc, a[0] if a else ROOT / "build/params")
    print(f"remote_params: {path}: {len(doc['parameters'])} parameters, Elixir {doc['firmwareVersion']} "
          f"({doc['sourceRevision']})")
