# Notice

Elixir is free software under the GNU General Public License, version 3 only (`LICENSE`).

It is derived from:

- **LXR-02 firmware**, Copyright (C) 2012–2026 Erica Synths, GPL-3.0 (`LICENSES/GPL-3.0-LXR.txt`), which in turn builds on Julian Schmidt's original Sonic Potions LXR. Elixir's sound engine, parameters, kit format, sequencer, MIDI handling and front-panel functions are ports of its sources. The ported files name the originals they follow.
- **Felucca**, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, GPL-3.0. Elixir uses its FM-1 hardware layer (`firmware/hal`), boot, LCD, graphics, USB, storage and update code, its reverb (`firmware/src/reverb.c`, from Felucca's `fx.c`), its build and packaging tools, and its browser emulator page. Changed files say what Elixir changed.

- **Grids**, Copyright (C) 2011–2012 Emilie Gillet, GPL-3.0-or-later (used under GPL-3.0). The GRIDS generator's drum map (`firmware/src/grids_map.h`, generated from Grids' `resources.cc` by `tools/gen_grids.py`) and its reading of the map (`firmware/src/seq.c` `grids_level`, `grids_step`) follow Grids' `pattern_generator.cc`.

- **Rings** and **stmlib**, Copyright 2012–2015 Emilie Gillet, MIT licence (`LICENSES/MIT-Rings.txt`). The DRUM voices' models (`firmware/src/rings.c`, its tables `firmware/src/rings_tables.h` from `tools/gen_rings_tables.py`) are a fixed-point port of Rings' modal resonator, string and FM voice, its exciters, limiter and the stmlib DSP they use.

- **Plaits** and **stmlib**, Copyright 2016–2023 Emilie Gillet, MIT licence (`LICENSES/MIT-Plaits.txt`). The DRUM voices' P models (`firmware/src/plaits.c`, `firmware/src/plaits_va.c`, the tables `firmware/src/plaits_tables.h` from `tools/gen_plaits_tables.py`) are a fixed-point port of Plaits' sixteen original engines and the oscillators, drums, noise, physical models, speech and stmlib DSP they use.

- **Marbles**, Copyright 2015 Emilie Gillet, MIT licence (`LICENSES/MIT-Marbles.txt`). The MELODY generator (0.16, `firmware/src/seq.c` `mel_note`) follows the design of Marbles' X section: SPREAD, BIAS, STEPS (the scale's degrees thinned to its stronger ones) and DÉJÀ VU with its loop. It is written for Elixir; no Marbles code is copied.

- **Clouds**, Copyright 2014 Emilie Gillet, MIT licence (`LICENSES/MIT-Clouds.txt`). The reverb's PLATE model (0.16, `firmware/src/reverb.c` `rev_plate`) is Clouds' reverb (`clouds/dsp/fx/reverb.h`, on its `fx_engine.h`), Jon Dattorro's plate in the Griesinger topology, ported to fixed point at Elixir's half rate with its delay lengths scaled; `tests/reverb_compare.cc` checks it against Clouds' own code.

- **GLITCH** (ttreu, github.com/ttreu/fm1-glitch), an FM-1 firmware on M-VAVE's FM1 Open sources, GPL-3.0-only (with a linking exception that Elixir doesn't need and doesn't use). The MICRO types (0.17, `firmware/src/micro.c`) follow the design of its microsound sources in `app/glitch_dsp.c`: their shapes, lengths, colours and relative levels. They are written for Elixir in fixed point.

- **The second core** (0.15): starting the FM-1's second core and guarding against units whose second core misreads (`firmware/hal/fm1_cpu1.h`, `fm1_cpu1.S`, from Felucca's hardware layer) come from **X0X** (Charles Vestal, github.com/charlesvestal/fm1-x0x), **Melodee** (keremimo, github.com/keremimo/melodee) and **Jangada** (zednaked, github.com/zednaked/jangada), all GPL-3.0.

Other parts:

- Inter Tight (UI font), SIL Open Font License 1.1 (`LICENSES/OFL-InterTight.txt`).
- DotGothic16 (emulator page), SIL Open Font License 1.1 (`LICENSES/OFL-DotGothic16.txt`).
- The firmware package embeds JieLi AC79 SDK boot files under Apache-2.0 (`LICENSES/Apache-2.0.txt`).
- **LXR-02 kits** (0.19, `assets/kits`): the LXR-02's factory kits (built into the firmware) and Hrtl's DLC and UNTZ kit packs (offered by the install and library pages and the emulators) are the work of Erica Synths and Hrtl, distributed with Erica Synths' permission. See `assets/kits/README.md` for where they come from.

"Erica Synths" and "LXR-02" are trademarks of Erica Synths. "M-VAVE" and "FM-1" are trademarks of their owner. Elixir is an unofficial port, not affiliated with or endorsed by Erica Synths, M-VAVE or Hügelton Instruments.
