# Parked features

Code taken out of the firmware for flash, kept here so it can come back.

## 35i: PULSAR, WAVELET, SPLINTER (parked in 0.23-test5)

Three MICRO types after Curtis Roads' *Microsound*, built in 0.20 (Phase 35i), removed to make room for WANDER (about 2 KB of flash). A kit that used one now plays its LXR voice (kit_sanitize: the model is past the list).

To bring them back: `git apply docs/parked/35i-microsound.patch` from the repository root. It restores micro.c's three types, NMICRO 13 (rings.c), the names (params.c), the pictures and lines (ui_viz.c), render_test's names, and the manual and README text. The patch is against 0.23-test5; later edits to those files may need it applied by hand.

## P WTABLE: Plaits' wavetable engine (parked in 0.24-test3)

Plaits' sixth engine, built in 0.15, given its own pictures and knob names in 0.23. Taken out to free flash: its
192-wave table `PL_WAVES` alone is 50.7 KB. **Saved 49,016 B** (image 581,088 → 532,072 B of the 581,564 B slot).
P CHORD, which plays 15 of those waves, is kept unchanged (bit-exact: render_test checks it).

Unlike 35i it isn't a patch: the code stays in the tree behind a build flag, so it keeps compiling and its tests keep
running.

- **The flag:** `ELIXIR_WTABLE` (firmware/src/rings.c, default 0).
- **Off (the release):** `wt_*` (plaits_va.c), the `pl_t` union member and engine cases (plaits.c), the knob names
  (params.c), `viz_wtable` / `wv_wtable` / `wv_bank_txt` / `wv_pos` and `pl_card`'s WTABLE part (ui_viz.c) and
  `PL_WAVES` aren't built. P CHORD reads its own `PL_CHW` (3,960 B). The model number is kept (PE_WTBL, in saved
  kits): `model_ok` (sound.c) refuses it, the MODEL knob steps past it (`model_step`, ui.c), `kit_sanitize` (kit.c)
  puts a kit's P WTABLE on P ADDTV, a lock or morph to it plays the LXR's, and SPELLS and WANDER, which check
  `model_ok`, don't pick it.
- **On:** everything as in 0.23; `PL_CHW` isn't built and P CHORD reads its waves from `PL_WAVES` (`PL_CHW_AT`,
  plaits_va.c), so nothing is stored twice. In 0.24 that build is 581,104 B: it still fits, with 460 B left.

**To bring it back:**

1. Firmware: `ELIXIR_WTABLE=1 PYTHON=.venv/bin/python ./build.sh` (tools/build.py passes `-DELIXIR_WTABLE=1`).
   To make it the default instead, set `#define ELIXIR_WTABLE 1` in rings.c. For the emulator, add
   `-DELIXIR_WTABLE=1` to web/emu/build.sh's compile line; for the host tests, to tests/ci_tests.sh's two `cc` lines.
2. Tests: `cc -O1 -DELIXIR_WTABLE=1 -I build/gen -I firmware/src tests/ui_test.c -o build/ui_test -lm -w`, and the
   same for render_test. ui_test then also draws `viz-model-pwtable*` and checks the Bank/Bright/Family cards;
   render_test renders P WTABLE with the other engines.
3. Tables: firmware/src/plaits_tables.h holds both tables already, under `#if`. If it is regenerated
   (`EURORACK=<checkout of pichenettes/eurorack> python3 tools/gen_plaits_tables.py`), the generator writes both.
4. Docs to restore (from the 0.23 tag: `git show v0.23:MANUAL.md`): MANUAL.md's P WTABLE row in the Plaits table,
   the Env note (on P WTABLE it opens Bright alone), the "waves on the MODEL page" paragraph, and "sixteen engines"
   in MANUAL.md, README.md and web/index.html. The cheat sheet never listed it.
5. Check RAM: P WTABLE's state (`pl_wt_t`, 60 B) is smaller than the union's largest member, so no SRAM change.
