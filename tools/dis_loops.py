#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Elixir: the per-sample loops in build/elixir.dis, by size (the device's cost of a DSP loop, see
docs/TECHNICAL_DOCUMENTATION.md 11.3: 1% of a core ~ 80 instructions a sample).

  tools/dis_loops.py                 every loop of 20+ instructions, largest first
  tools/dis_loops.py en_osc svf_block  those functions' loops
  tools/dis_loops.py -s svf_block 0x2011a72   one loop's instructions

Columns: instructions, the shortest way round (head to back edge: the common path when the rest is rare branches,
an edge's blep, a wrap), function, the loop's start, stack accesses in it (spills), hardware divides in it (each
costs many cycles), calls in it, its end test.
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


def shortest(ins, tg, end):
    """the fewest instructions from the loop's head round to its back edge (a goto out of the loop: a dead end)"""
    from collections import deque
    addrs = [a for a, _ in ins if tg <= a <= end]
    text = dict((a, t) for a, t in ins)
    nxt = dict(zip(addrs, addrs[1:]))
    dist, q = {tg: 1}, deque([tg])
    while q:
        a = q.popleft()
        if a == end:
            return dist[a]
        t = text[a]
        outs = []
        m = re.search(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >", t)
        if m:
            b = int(m.group(1), 16)
            if tg <= b <= end and b in text:
                outs.append(b)
        if not (t.startswith("goto") or "= [sp++]" in t and "pc" in t or t.startswith("rts")) and a in nxt:
            outs.append(nxt[a])
        for b in outs:
            if b not in dist:
                dist[b] = dist[a] + 1
                q.append(b)
    return 0


def loops(name, ins):
    for ad, t in ins:
        if re.search(r"goto -\d+", t):
            m = re.search(r": ([0-9a-f]+) >", t)
            if not m:
                continue
            tg = int(m.group(1), 16)
            body = [x for a, x in ins if tg <= a <= ad]
            yield tg, body, t.split("goto")[0].strip(), ad


def main(argv):
    fn = functions()
    if argv[:1] == ["-s"]:
        name, start = argv[1], int(argv[2], 16)
        for tg, body, _, _ in loops(name, fn[name]):
            if tg == start:
                print("\n".join("  " + b for b in body))
        return
    rows = []
    for name in argv or fn:
        for tg, body, test, end in loops(name, fn[name]):
            if len(body) >= (1 if argv else 20) and len(body) < 600:
                syms = sorted(set(re.findall(r"call -?\d+ <([A-Za-z_]\w*)", " ".join(body))))
                spill = sum(("[sp" in x or "sp +" in x) for x in body)
                div = sum(bool(re.search(r"= r\d+ / r\d+", x)) for x in body)
                rows.append((len(body), shortest(fn[name], tg, end), name, hex(tg), spill, div, ",".join(syms), test))
    rows.sort(key=lambda r: -r[0])
    for r in rows:
        print("%4d  min %-4d %-22s %s  sp %-3d div %-2d calls %-30s %s" % r)


if __name__ == "__main__":
    main(sys.argv[1:])
