#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Elixir 0.19: the Hrtl kit packs (assets/kits/hrtl-*, distributed with Erica Synths' permission: assets/kits/README.md)
# as kit library files (.ktlb, web/library.js packKitLib's format), for the install page, the library page and the
# emulators to put into the kit library's empty slots. web/library.js addPacks puts each kit into the first empty slot
# from L1; the slots written here only keep the kits' order.
#
#   tools/gen_kit_packs.py OUT_DIR      (web/emu/build.sh: build/emu/kits)
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from gen_factory_kits import kits_in   # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PACKS = [("hrtl-dlc", 96), ("hrtl-untz", 112)]      # (folder, first library slot, 0-based)
KL_KIT = 1


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: gen_kit_packs.py OUT_DIR")
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for name, first in PACKS:
        kits = list(kits_in(ROOT / "assets/kits" / name))
        if not kits or first + len(kits) > 128:
            sys.exit(f"gen_kit_packs: {name}: {len(kits)} kits from slot {first + 1}")
        b = bytearray(b"KTLB" + struct.pack("<I", len(kits)))
        for i, (kname, data, _) in enumerate(kits):
            b += bytes([KL_KIT, first + i]) + kname.encode()[:8].ljust(8) + struct.pack("<H", len(data)) + data
        (out / f"{name}.ktlb").write_bytes(b)
        print(f"kit pack {name}: {len(kits)} kits, library {first + 1}-{first + len(kits)} -> {out / name}.ktlb")


if __name__ == "__main__":
    main()
