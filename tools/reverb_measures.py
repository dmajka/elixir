#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Elixir: measures of the reverb's impulse responses (docs/TO_DO_REVERBS.md step 0), from tests/bench/reverb_bench.c's
ir_<MODEL>_s<SIZE>.wav (stereo, 32-bit float, the return at LEVEL 1 to an impulse of 4.0) and irq_ (to 0.5).

  .venv/bin/python tools/reverb_measures.py build/rev/          the table
  .venv/bin/python tools/reverb_measures.py build/rev/ --spec   and a spectrogram per IR (spec_<MODEL>_s<SIZE>.png)

Columns (the mid is (L + R) / 2):
  pre     ms from the impulse to the first sound above -60 dB of the IR's peak
  RT60    seconds, per octave band (125 Hz .. 8 kHz), from the Schroeder decay curve's -5 .. -25 dB (T20 x 3)
  dense   ms after the first sound until the echo density (Abel and Huang's normalized echo density, 20 ms window)
          first reaches 0.9: how soon the tail is smooth (a sparse, flutter-y start: late or never)
  ned     the mean echo density 50 .. 300 ms (1: as dense as noise)
  width   1 - the left-right correlation of the tail (50 ms .. 1 s, the largest over +-1 ms): 0 mono, ~1 wide
  peaky   dB: the tail's averaged spectrum (Welch, 50 ms .. 1.5 s) against itself smoothed over a third of an octave,
          the deviations' standard deviation, 200 Hz .. 8 kHz: how much it rings at pitches (metallic: high)
  level   dB: the mid's RMS over its first second, against PLATE's at the same SIZE (the loud IR)
  q/l     RT60 at 1 kHz of the quiet IR over the loud's: below 1, a quiet send's tail is cut short by the rounding
"""
import glob
import os
import re
import struct
import sys

import numpy as np

FS = 44100
BANDS = [125, 250, 500, 1000, 2000, 4000, 8000]


def read_wav(path):
    d = open(path, "rb").read()
    assert d[:4] == b"RIFF" and d[8:12] == b"WAVE"
    p, fmt, ch, data = 12, None, None, None
    while p < len(d):
        cid, n = d[p:p + 4], struct.unpack("<I", d[p + 4:p + 8])[0]
        if cid == b"fmt ":
            fmt, ch = struct.unpack("<HH", d[p + 8:p + 12])
        elif cid == b"data":
            data = d[p + 8:p + 8 + n]
        p += 8 + n
    x = np.frombuffer(data, "<f4" if fmt == 3 else "<i2").astype(np.float64)
    if fmt != 3:
        x /= 32768.0
    return x.reshape(-1, ch)


def octave(x, fc):
    """x band-passed to the octave around fc (a raised cosine on log frequency, in the FFT: zero phase)"""
    n = 1 << int(np.ceil(np.log2(len(x) * 2)))
    f = np.fft.rfftfreq(n, 1 / FS)
    u = np.log2(np.maximum(f, 1e-9) / fc)
    w = np.where(np.abs(u) < 1, 0.5 + 0.5 * np.cos(np.pi * u), 0)
    return np.fft.irfft(np.fft.rfft(x, n) * w, n)[:len(x)]


def rt60(x):
    e = np.cumsum((x ** 2)[::-1])[::-1]
    if e[0] <= 0:
        return float("nan")
    edc = 10 * np.log10(np.maximum(e / e[0], 1e-30))
    i5, i25 = np.argmax(edc <= -5), np.argmax(edc <= -25)
    if edc[-1] > -25 or i25 <= i5:
        return float("nan")
    t = np.arange(i5, i25) / FS
    slope = np.polyfit(t, edc[i5:i25], 1)[0]
    return -60 / slope if slope < 0 else float("nan")


def ned(x, win_ms=20):
    """Abel and Huang's normalized echo density, a Hann window sliding by 1 ms"""
    w = np.hanning(int(FS * win_ms / 1000))
    w /= w.sum()
    hop, half, out = FS // 1000, len(w) // 2, []
    for c in range(half, len(x) - half, hop):
        s = x[c - half:c - half + len(w)]
        sd = np.sqrt(np.sum(w * s * s))
        out.append(np.sum(w * (np.abs(s) > sd)) / 0.3173 if sd > 0 else 0.0)
    return np.array(out), half / FS


def welch_db(x, n=4096):
    win, segs = np.hanning(n), []
    for s in range(0, len(x) - n, n // 2):
        segs.append(np.abs(np.fft.rfft(x[s:s + n] * win)) ** 2)
    return 10 * np.log10(np.mean(segs, axis=0) + 1e-30), np.fft.rfftfreq(n, 1 / FS)


def peakiness(x):
    p, f = welch_db(x)
    lin = 10 ** (p / 10)
    sm = np.empty_like(p)
    for i, fi in enumerate(f):
        lo, hi = np.searchsorted(f, fi * 2 ** (-1 / 6)), np.searchsorted(f, fi * 2 ** (1 / 6))
        sm[i] = 10 * np.log10(np.mean(lin[lo:max(hi, lo + 1)]) + 1e-30)
    m = (f >= 200) & (f <= 8000)
    return float(np.std((p - sm)[m]))


def measure(ir):
    mid = (ir[:, 0] + ir[:, 1]) / 2
    pk = np.max(np.abs(mid))
    on = int(np.argmax(np.abs(mid) > pk * 1e-3))
    h = mid[on:]
    r = {"pre": on / FS * 1000, "rt": [rt60(octave(h, fc)) for fc in BANDS]}
    d, t0 = ned(h[:int(FS * 1.0)])
    hit = np.nonzero(d >= 0.9)[0]
    r["dense"] = (hit[0] / 1000 + t0) * 1000 if len(hit) else float("nan")
    r["ned"] = float(np.mean(d[50:300]))
    a, b = ir[on + int(FS * .05):on + FS, 0], ir[on + int(FS * .05):on + FS, 1]
    lag, c = int(FS / 1000), 0.0
    for k in range(-lag, lag + 1):
        u, v = (a[k:], b[:len(b) - k]) if k >= 0 else (a[:k], b[-k:])
        den = np.sqrt(np.sum(u * u) * np.sum(v * v))
        c = max(c, abs(np.sum(u * v)) / den if den > 0 else 1.0)
    r["width"] = 1 - c
    r["peaky"] = peakiness(h[int(FS * .05):int(FS * 1.5)])
    r["rms"] = float(np.sqrt(np.mean(mid[:FS] ** 2)))
    return r


def spectrogram(ir, path, secs=3.0, n=1024, hop=128):
    from PIL import Image
    x = (ir[:int(FS * secs), 0] + ir[:int(FS * secs), 1]) / 2
    win = np.hanning(n)
    cols = [np.abs(np.fft.rfft(x[s:s + n] * win)) for s in range(0, len(x) - n, hop)]
    db = 20 * np.log10(np.array(cols).T + 1e-12)
    db = np.clip((db - db.max() + 80) / 80, 0, 1)    # 80 dB of range
    img = (255 * (1 - db[::-1])).astype(np.uint8)    # (low frequencies at the bottom, loud dark)
    Image.fromarray(img).resize((img.shape[1], 512)).save(path)


def main(args):
    d = args[0] if args else "build/rev/"
    res = {}
    for p in sorted(glob.glob(os.path.join(d, "ir_*_s*.wav"))):
        m = re.match(r"ir_(\w+?)_s(\d+)\.wav", os.path.basename(p))
        ir = read_wav(p)
        res[(m.group(1), int(m.group(2)))] = r = measure(ir)
        q = os.path.join(d, "irq" + os.path.basename(p)[2:])
        r["ql"] = rt60(octave(np.mean(read_wav(q), axis=1), 1000)) / r["rt"][3] if os.path.exists(q) else float("nan")
        if "--spec" in args:
            spectrogram(ir, os.path.join(d, "spec_%s_s%s.png" % (m.group(1), m.group(2))))
    order = ["ROOM", "SPRING", "HALL", "PLATE"]
    models = [k for k in order if any(k == a for a, _ in res)] + sorted({a for a, _ in res} - set(order))
    print("%-8s %4s %6s  %s  %6s %5s %6s %6s %6s %5s" % ("model", "SIZE", "pre", " ".join("%5s" % (
        "%dk" % (b // 1000) if b >= 1000 else b) for b in BANDS), "dense", "ned", "width", "peaky", "level", "q/l"))
    for z in sorted({z for _, z in res}):
        ref = res.get(("PLATE", z))
        for mdl in models:
            r = res.get((mdl, z))
            if not r:
                continue
            lvl = 20 * np.log10(r["rms"] / ref["rms"]) if ref and ref["rms"] > 0 else float("nan")
            print("%-8s %4d %5.1fms %s  %5.0fms %5.2f %6.2f %5.1fdB %+5.1fdB %5.2f" % (
                mdl, z, r["pre"], " ".join("%5.2f" % v for v in r["rt"]), r["dense"], r["ned"], r["width"],
                r["peaky"], lvl, r["ql"]))
        print()


if __name__ == "__main__":
    main(sys.argv[1:])
