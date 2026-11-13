#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Elixir: the per-sample loops in build/elixir.dis, by size (the device's cost of a DSP loop, see
docs/TECHNICAL_DOCUMENTATION.md 11.3: 1% of a core ~ 80 instructions a sample).

  tools/dis_loops.py                 every loop of 20+ instructions, largest first
  tools/dis_loops.py en_osc svf_block  those functions' loops
  tools/dis_loops.py -s svf_block 0x2011a72   one loop's instructions

Columns: instructions, function, the loop's start, stack accesses in it (spills), calls in it, its end test.
A loop is a backward goto; its body is the instructions from the target to it. Functions inlined into a caller show
under the caller (Plaits' engines under pl_engine_render, the Rings models under voices_render)."""
import re
import sys

DIS = "build/elixir.dis"


def functions(path=DIS):
    fn, cur = {}, None
    for line in open(path):
        line = line.rstrip("\n")
        m = re.match(r"^([A-Za-z_][\w.]*):$", line)
        if m and not m.group(1).startswith("."):
            cur = m.group(1)
            continue
        m = re.match(r"\s*([0-9a-f]{7}):\s+((?:[0-9a-f]{2} )+)\s*(.*)", line)
        if m and cur:
            fn.setdefault(cur, []).append((int(m.group(1), 16), m.group(3)))
    return fn


def loops(name, ins):
    for ad, t in ins:
        if re.search(r"goto -\d+", t):
            m = re.search(r": ([0-9a-f]+) >", t)
            if not m:
                continue
            tg = int(m.group(1), 16)
            body = [x for a, x in ins if tg <= a <= ad]
            yield tg, body, t.split("goto")[0].strip()


def main(argv):
    fn = functions()
    if argv[:1] == ["-s"]:
        name, start = argv[1], int(argv[2], 16)
        for tg, body, _ in loops(name, fn[name]):
            if tg == start:
                print("\n".join("  " + b for b in body))
        return
    rows = []
    for name in argv or fn:
        for tg, body, test in loops(name, fn[name]):
            if len(body) >= (1 if argv else 20) and len(body) < 600:
                syms = sorted(set(re.findall(r"call -?\d+ <([A-Za-z_]\w*)", " ".join(body))))
                spill = sum(("[sp" in x or "sp +" in x) for x in body)
                rows.append((len(body), name, hex(tg), spill, ",".join(syms), test))
    rows.sort(key=lambda r: -r[0])
    for r in rows:
        print("%4d  %-22s %s  sp %-3d calls %-30s %s" % r)


if __name__ == "__main__":
    main(sys.argv[1:])
