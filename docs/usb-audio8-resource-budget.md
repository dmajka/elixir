# 8-channel USB audio: resource budget (Phase 8, 2026-11-06)

Deliverable 2 of the PRD. Reproduce with `PYTHON=$PWD/.venv/bin/python tools/usb_audio8_budget.sh be4c41e` (builds
the pre-feature commit in a scratch worktree, then `ELIXIR_USB_AUDIO=0` and `=8`; ELFs and logs in build/usb8/).
Compiler: JieLi pi32v2 clang `-Os`, tools/build.py's flags; 16-bit samples, 64-frame blocks, 128-frame halves,
736 B packets.

## Measured (static, from the linker and the ELF)

| Metric | A: be4c41e | `ELIXIR_USB_AUDIO=0` (UI page only) | D: `=8` (transport, stems, UI) | D − A |
|---|---:|---:|---:|---:|
| Image (flash) | 559,776 | 560,260 | 562,140 | **+2,364** |
| Flash free (of 581,564) | 21,788 | 21,304 | 19,424 | |
| RAM .data+.bss (of 73,728) | 69,872 | 69,872 | 70,736 | **+864** |
| Pool (of 380,928) | 344,428 | 344,428 | 350,572 | **+6,144** |
| RAMTEXT free | 96 | 96 | 96 | 0 |
| SRAM above the 8 KiB reserve | 32,164 | 32,164 | 25,156 | **−7,008** |
| Heap | none (Elixir has no heap) | | | 0 |

The PRD's configurations B (transport without stems) and C (stems without UI) weren't built separately: the stems are
two loops inside existing functions and B would be a build nobody ships. Their split comes from the symbols:

| Symbol (D) | Bytes | Where |
|---|---:|---|
| `ua_ring` | 6,144 | pool: 384 frames × 8 ch × 2 B |
| `ep4tx` | 736 | RAM: the DMA packet |
| `uac` (state) | 76 | RAM |
| `ua_last`, `ua_w`, `ua_r`, `uac_ticks`, load window, `mix_out` | 41 | RAM |
| `uac_service` (with `uac_packet` inlined) | 610 | flash |
| `uac_render_start` | 192 | flash |
| `uac_load_pm`, `uac_stream`, `uac_ep4_reset` | 204 | flash |
| descriptors, stem/mix loops, CONFIG page, help, LOAD line | rest of +2,364 | flash |

**Stacks:** new frames are small: `uac_service` 52 B (13 registers), nested in TIMER5 (72 B) inside the audio ISR;
`voices_block` gains one pointer. The system stack's static worst case was ~3.5 KB of 8 KB (1.0 Phase 1); the unit's
SYS STACK line reads the real high-water.

**DMA / USB reserved RAM:** `ep4tx` 736 B (the only DMA buffer added); the ring is CPU-only.

## Runtime (estimates; to read on the unit)

| Metric | Value | Evidence |
|---|---|---|
| Render-side cost | 512 stores a 64-frame block, ~0.2% of core 0 (under MIXER) | code |
| Service cost | est. 1–3% of core 0 while recording (2 SIE accesses a 0.1 ms tick, a 736 B copy a ms); 0 when not | **measured on the unit by CONFIG → LOAD → USB AUDIO** |
| Latency | ~4 ms: a frame waits for the ring's fill at a half's start (96..144 frames) plus its place in the half (0..127) | design |
| Underruns / overruns / missed | counted since power-on: LOAD's "glitches" | `uac.underruns + overruns + missed` |

The audio ISR's own LOAD figures don't include TIMER5's nested time (by design: `load_clock`), so the USB service has
its own line.

## Headroom and verdict

After the feature: **19.4 KB flash free, 25.2 KB SRAM above the 8 KiB reserve** (2,992 B of RAM proper, 30,356 B of
pool). Phase 7 (the release) needs ~0 of either; Phase 9's lock pool frees RAM. The feature costs far less than the
PRD's planning range at the time ("−9–10 KB" SRAM) because the stems are written straight into the transmit ring
(no per-voice 32-bit buffers, no staging copy).

**Verdict: ship now, enabled (`ELIXIR_USB_AUDIO 8`), pending the user's hardware test** (TO_DO Phase 8's last item).
`ELIXIR_USB_AUDIO=0` is the rollback: MIDI only, RAM identical to before, +484 B flash for the CONFIG page.

16 vs 24 bit: 24-bit would be 46 × 8 × 3 = 1,104 B a packet, over full speed's 1,023 B isochronous limit; 8 channels
at 24 bit can't be done on this USB controller (full speed only). 16-bit loses nothing audible here: the LXR engine
and the DAC path are 16-bit (`sat16` before the output).
