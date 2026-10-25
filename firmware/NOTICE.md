# Notice

Kettle is free software under the GNU General Public License, version 3 only (`LICENSE`).

It is derived from:

- **LXR-02 firmware**, Copyright (C) 2012–2026 Erica Synths, GPL-3.0 (`LICENSES/GPL-3.0-LXR.txt`), which in turn builds on Julian Schmidt's original Sonic Potions LXR. Kettle's sound engine, parameters, kit format, sequencer, MIDI handling and front-panel functions are ports of its sources. The ported files name the originals they follow.
- **Felucca**, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, GPL-3.0. Kettle uses its FM-1 hardware layer (`firmware/hal`), boot, LCD, graphics, USB, storage and update code, its reverb (`firmware/src/reverb.c`, from Felucca's `fx.c`), its build and packaging tools, and its browser emulator page. Changed files say what Kettle changed.

- **Grids**, Copyright (C) 2011–2012 Emilie Gillet, GPL-3.0-or-later (used under GPL-3.0). The GRIDS generator's drum map (`firmware/src/grids_map.h`, generated from Grids' `resources.cc` by `tools/gen_grids.py`) and its reading of the map (`firmware/src/seq.c` `grids_level`, `grids_step`) follow Grids' `pattern_generator.cc`.

- **Rings** and **stmlib**, Copyright 2012–2015 Emilie Gillet, MIT licence (`LICENSES/MIT-Rings.txt`). The DRUM voices' models (`firmware/src/rings.c`, its tables `firmware/src/rings_tables.h` from `tools/gen_rings_tables.py`) are a fixed-point port of Rings' modal resonator, string and FM voice, its exciters, limiter and the stmlib DSP they use.

Other parts:

- Inter Tight (UI font), SIL Open Font License 1.1 (`LICENSES/OFL-InterTight.txt`).
- DotGothic16 (emulator page), SIL Open Font License 1.1 (`LICENSES/OFL-DotGothic16.txt`).
- The firmware package embeds JieLi AC79 SDK boot files under Apache-2.0 (`LICENSES/Apache-2.0.txt`).

"Erica Synths" and "LXR-02" are trademarks of Erica Synths. "M-VAVE" and "FM-1" are trademarks of their owner. Kettle is an unofficial port, not affiliated with or endorsed by Erica Synths, M-VAVE or Hügelton Instruments.
