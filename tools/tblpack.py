#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Elixir (22): lookup tables packed in flash, unpacked into RAM at boot (firmware/src/tblpack.c).

  tools/tblpack.py OUT.h HEADER.h ...

Each header marks a packed table as two things: the table itself, a plain RAM array (`static T NAME[n] TBL_RAM;`,
the pool on the unit), and its values under `#if TBL_RAW` as `static const T NAME_raw[n] = {...};` (built only by
the host tests, which check the unpacked table against them). This reads every NAME_raw, packs it and writes OUT.h:
the packed bytes (tbl_bits) and the list of tables (TBL_LIST) with each one's offset, code and CRC-32.

The code, lossless: the values as w-bit integers (w the element's width), differenced `order` times (0..3) modulo
2^w, each difference as a signed number zigzagged (0, -1, 1, -2 .. -> 0, 1, 2, 3 ..) and Rice coded with parameter k:
u >> k in unary (that many 1s, then a 0), then u's low k bits; from QMAX 1s on, the 1s stop and u follows in 32 bits.
Bits are read from each byte's low bit up. The order and k are chosen per table for the fewest bytes.
"""
import re
import sys
import zlib
from pathlib import Path

QMAX = 24
WIDTH = {"int8_t": 1, "uint8_t": 1, "int16_t": 2, "uint16_t": 2, "int32_t": 4, "uint32_t": 4}
RAW = re.compile(r"static const (\w+) (\w+)_raw\[(\d+)\] = \{(.*?)\};", re.S)


def packed_arr(name, ctype, vals, per=12):
    """a packed table as the generators write it into a header (which includes tblpack.h): the RAM array, then
    its values for this tool and the host tests"""
    out = [f"static {ctype} {name}[{len(vals)}] TBL_RAM;   /* (22: packed, unpacked at boot: tblpack.c) */",
           "#if TBL_RAW", f"static const {ctype} {name}_raw[{len(vals)}] = {{"]
    for i in range(0, len(vals), per):
        out.append("    " + ", ".join(str(int(v)) for v in vals[i:i + per]) + ",")
    return out + ["};", "#endif"]


def raw_tables(paths):
    """[(name, ctype, values)] of every NAME_raw in the headers, in order"""
    out = []
    for p in paths:
        for m in RAW.finditer(Path(p).read_text()):
            vals = [int(t) for t in m.group(4).replace("\n", " ").split(",") if t.strip()]
            if len(vals) != int(m.group(3)) or m.group(1) not in WIDTH:
                raise SystemExit(f"tblpack: {p}: {m.group(2)}_raw: {len(vals)} values of {m.group(3)} ({m.group(1)})")
            out.append((m.group(2), m.group(1), vals))
    return out


def residuals(vals, w, order):
    mask = (1 << 8 * w) - 1
    a = [v & mask for v in vals]
    for _ in range(order):
        a = [a[0]] + [(a[i] - a[i - 1]) & mask for i in range(1, len(a))]
    half = 1 << (8 * w - 1)
    return [x - (mask + 1) if x >= half else x for x in a]


def zigzag(r):
    return ((r << 1) ^ (r >> 31)) & 0xFFFFFFFF


def rice(res, k):
    bits = []
    for r in res:
        u = zigzag(r)
        q = u >> k
        if q < QMAX:
            bits += [1] * q + [0] + [(u >> i) & 1 for i in range(k)]
        else:
            bits += [1] * QMAX + [(u >> i) & 1 for i in range(32)]
    bits += [0] * (-len(bits) % 8)
    return bytes(sum(bits[i + j] << j for j in range(8)) for i in range(0, len(bits), 8))


def unpack(data, n, w, order, k):
    """the decoder as tblpack.c has it: the values as w-bit unsigned integers"""
    pos = 0

    def bit():
        nonlocal pos
        b = data[pos >> 3] >> (pos & 7) & 1
        pos += 1
        return b

    def bits(c):
        return sum(bit() << i for i in range(c))
    acc = [0, 0, 0]
    out = []
    for _ in range(n):
        q = 0
        while bit():
            q += 1
            if q == QMAX:
                break
        u = bits(32) if q == QMAX else (q << k) | bits(k)
        x = ((u >> 1) ^ -(u & 1)) & 0xFFFFFFFF
        for j in range(order - 1, -1, -1):          # order prefix sums, one accumulator each
            acc[j] = (acc[j] + x) & 0xFFFFFFFF
            x = acc[j]
        out.append(x & ((1 << 8 * w) - 1))
    return out


def pack(vals, w):
    """the fewest bytes over order 0..3 and k 0..24: (bytes, order, k)"""
    best = None
    for order in range(4):
        res = residuals(vals, w, order)
        for k in range(25):
            if best and sum(min(zigzag(r) >> k, QMAX) for r in res) // 8 > len(best[0]):
                continue
            b = rice(res, k)
            if not best or len(b) < len(best[0]):
                best = (b, order, k)
    return best


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    tables = raw_tables(sys.argv[2:])
    blob, rows, raw_total = bytearray(), [], 0
    for name, ctype, vals in tables:
        w = WIDTH[ctype]
        data, order, k = pack(vals, w)
        mask = (1 << 8 * w) - 1
        if unpack(data, len(vals), w, order, k) != [v & mask for v in vals]:
            raise SystemExit(f"tblpack: {name} does not unpack to its values")
        raw = b"".join((v & mask).to_bytes(w, "little") for v in vals)
        rows.append((name, len(blob), order << 5 | k, zlib.crc32(raw), len(raw), len(data)))
        blob += data
        raw_total += len(raw)
    if len(blob) > 0xFFFF:
        raise SystemExit("tblpack: the packed bytes outgrow tblpack.c's 16-bit offsets")
    L = ["/* generated by tools/tblpack.py: do not edit. The tables packed in flash, unpacked into RAM at boot",
         " * (firmware/src/tblpack.c): X(NAME, offset in tbl_bits, order << 5 | Rice k, CRC-32 of the unpacked bytes) */",
         "#pragma once", f"#define TBL_QMAX {QMAX}u", f"#define TBL_N {len(rows)}u",
         f"#define TBL_RAM_BYTES {raw_total}u", "#define TBL_LIST(X) \\"]
    for name, off, ok, crc, nraw, npk in rows:
        L.append(f"    X({name}, {off}u, {ok}u, 0x{crc:08X}u)   /* {nraw} B -> {npk} B */ \\")
    L.append("")
    L.append(f"static const uint8_t tbl_bits[{len(blob)}] = {{")
    for i in range(0, len(blob), 20):
        L.append("    " + ", ".join(str(b) for b in blob[i:i + 20]) + ",")
    L.append("};")
    Path(sys.argv[1]).write_text("\n".join(L) + "\n")
    print(f"tblpack: {len(rows)} tables, {raw_total} B in RAM from {len(blob)} B packed")


if __name__ == "__main__":
    main()
