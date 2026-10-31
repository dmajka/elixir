# LXR-02 kits

Kits for the Erica Synths LXR-02, distributed with Elixir with Erica Synths' permission (2026). They are the work of
Erica Synths and Hrtl, not part of Elixir's own GPL-3.0 code.

- `factory/` — the 85 distinct kits of the LXR-02 factory SD image (06.07), Init and empty kits and repeats left out.
  Built into the firmware as the FACTORY bank (`tools/gen_factory_kits.py` → `build/gen/factory_kits.h`).
- `hrtl-dlc/` — Hrtl's LXR-02 DLC kits (16).
- `hrtl-untz/` — Hrtl's LXR-02 UNTZ kits (16).
  Both made into kit library files (`tools/gen_kit_packs.py` → `build/emu/kits/*.ktlb`) that the install page, the
  library page and the emulators put into the kit library's empty slots (DLC 97–112, UNTZ 113–128).

Each file is an LXR-02 `.SND` kit (8 name bytes, the LXR's 247 sound parameters), renamed `NNN-NAME.SND` in the order
the bank uses.

The originals, and the instrument:

- Erica Synths LXR-02: https://www.ericasynths.lv/drum-synthesizer-lxr-02-1132/
- Factory content: https://www.ericasynths.lv/service/file/download/product_id/754/file_id/418/
- Hrtl DLC: https://www.ericasynths.lv/service/file/download/product_id/754/file_id/462/
- Hrtl UNTZ: https://www.ericasynths.lv/service/file/download/product_id/754/file_id/625/
