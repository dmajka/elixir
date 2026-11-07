# 8-channel USB audio: investigation (Phase 8, 2026-11-06)

Deliverable 1 of docs/Elixir_Reusable_8_Channel_USB_Audio_PRD.md. Companion docs: usb-audio8-resource-budget.md,
usb-audio8-routing.md, usb-audio8-module-design.md, usb-audio8-memory-roadmap.md.

## 1. Provenance

| Source | Where | Commit | Licence |
|---|---|---|---|
| Elixir (this repo) before the feature | `main` | `be4c41e` (1.0 Phase 6) | GPL-3.0-only |
| Felucca, FNTSMG-1's branch | github.com/fantasmogenesis/Felucca, branch `groovebox` | `31d9afa` (2026-11-05) | GPL-3.0 |
| felucca-bsp (Felucca's hardware layer as a library) | github.com/hugelton/felucca-bsp | `5b0879b` (2026-11-06) | GPL-3.0 |
| fm1-base (10-channel app platform on felucca-bsp) | github.com/charlesvestal/fm1-base | `317cb86` (2026-11-06) | GPL-3.0 |
| karma-fm1 (6-channel STEMS mode, after JIANT FM) | github.com/eatyourgreensstudio-dotcom/karma-fm1 | `47b9b66` (2026-11-06) | GPL-3.0 |

The FNTSMG-1 ZIP named in the PRD wasn't needed: its `.gitmodules` points at Felucca's `groovebox` branch, which was
fetched directly. Elixir's usb.c is Felucca's (Leo Kuroshita, Hügelton Instruments); felucca-bsp's chip/usb.c is the
same file grown on. The code Elixir took (the multichannel ring, packet builder and stall count) is from felucca-bsp,
same author and licence, adapted; nothing was copied from FNTSMG-1's application code.

Hardware: JieLi AC79 (WL82, pi32v2, two cores at 360 MHz), USB0 full-speed device (MUSB-style SIE reached through
CON1, `hal/fm1_usb.h`), I2S at 6 MHz / 136 = 44,117.647 Hz. Toolchain: JieLi pi32v2 clang, `-Os` (tools/build.py).

## 2. How the others scale Felucca's stereo input

Felucca's stereo input (`FELUCCA_UAC`, Elixir usb.c, off in Elixir before 1.0): UAC1, one AudioStreaming interface
(alt 0 none, alt 1 the stream), Type I PCM 16 bit, EP4 IN isochronous **asynchronous**, 1 ms interval, no feedback
endpoint (the device sets the rate by sending 44 or 45 frames, ±1 to hold its ring's fill). Producer: the audio ISR
writes a ring of `uint32` frames; consumer: `uac_service` in TIMER5 queues one packet whenever the last has gone
(TxPktRdy clear), at 2 kHz and nested inside the render.

| | FNTSMG-1 (`FELUCCA_UAC_MT`) | felucca-bsp / fm1-base (`FELUCCA_UAC_CH` 4..10) | karma-fm1 (STEMS) | Elixir 1.0 |
|---|---|---|---|---|
| Host sees | one 8-ch input | one N-ch input | one 6-ch input (a separate device mode) | one 8-ch input |
| Channels | 8 tracks pre-mix, **no** mix | mix on 1-2, the app's on 3..N | 3 tracks + drums mono, FX return L R | mix on 1-2, six voices on 3-8 |
| Packet | 46 × 16 = 736 B (asserted ≤ 736: 10 ch "do not fit" at its 2 kHz service, usb.c:25-27, :113) | 92 N B, ≤ 920 | 552 B | 736 B |
| Service | every TIMER5 tick nested (main.c:48), plus outside (main.c:56) | every 10 kHz tick (fm1-base main_fm1.c:217-223: "at 2 kHz ... 38,400 frames/s instead of 44,100") | every tick | every tick (Elixir main.c:30-46) |
| Ring | `uint32_t ua_ring[512 × 4]` 8 KB | `int16_t ua_ring[UA_N][N]`, UA_N a whole number of halves (chip/usb.c:248) | 440 frames × 3 words | `int16_t ua_ring[384][8]` 6 KB, in the pool |
| Stems written | at tap time from `fgx_box.trk[8]` (an app buffer, >> 1) | the app adds into the ring while it renders (`uac_ch_frame`, chip/usb.c:1799) | from `stem_buf` | the app writes the ring a block at a time (`uac_block`) |
| bcdDevice | +0x40 | +0x20 (chip/usb.c:601) | its own | +0x20 → 3.38 |
| 48 kHz | off | off ("resampling N channels costs too much") | off | off |

All of them: UAC1, 16 bit, 44.1 kHz only, wChannelConfig L R (bsp) or none (FNTSMG-1), no channel names, the same EP4,
`TXMAXP` written 0xFF as in the stereo code (the EP4 DMA count register sets the packet length; 736 and 920 B
packets are shipped this way).

Answers to the PRD's Phase 1 questions:

- **What the host sees:** one 8-channel capture stream on one AudioStreaming interface (USB Audio Class 1.0).
- **Endpoint mode / fractional frames:** isochronous asynchronous IN, no explicit feedback; Elixir (as felucca-bsp)
  sends 44 frames a ms and one more every ~8.5 ms (`UA_FRAC44` 0.11765: the I2S clock), and steers the ring's fill
  by one frame a render at most (`uac.fresh`).
- **Buffers:** a static ring (pool, 6,144 B), a static packet buffer `ep4tx` (RAM, 736 B, the DMA source), `ua_last`
  16 B, state 76 B. No heap, no stack buffers.
- **Clock drift / host lag:** the device's clock leads; the ring's fill is held in 96..144 frames. If the host stops
  reading for 20 ms (200 polls) the stream counts as stalled, the render stops feeding, and it re-primes when packets
  move again; an empty ring repeats the last frame (underrun), a full one drops the half (overrun).
- **Physical output at the same time:** yes; the FM-1's DAC output is unchanged, and USB 1-2 carry its signal before
  the MASTER pot (usb-audio8-routing.md).
- **Other functions:** USB MIDI (EP1) and the OTA SysEx update path are untouched; Elixir builds no CDC console and no
  mass storage, so nothing else shares the bus. The update loader (FELUCCA_LOADER) builds no audio at all.

## 3. Elixir's implementation (file:line, as committed)

```
ALNK0 ISR (audio.c fm1_alnk0_irq, every 2.9 ms)
  audio.c:83        uac_render_start()          feed this half? prime? room for 128 frames?
  for each 64-frame block: engine_block()       (engine.c)
      voices_block()                            (voices.c)
        voices.c:1084   ub = uac_block()          NULL when nobody records
        per voice, after insert/xmod/choke/PUMP:
          stem -> ub[i*8 + 2 + v]
          voices.c:1124   OUT and recording: skip mix_voice (dry + sends)
      comp_master, dirt_master, sat16, perf_master
      engine.c:102    mix -> ub[i*8 + 0..1], uac_block_done()   ua_w += 64 (published)
TIMER5 (main.c fm1_timer5_irq, 10 kHz, nests into ALNK0)
  main.c:33 (nested) / :45 (otherwise) / usb_poll at 2 kHz:  uac_service()
      usb.c:1397  timed (CONFIG > LOAD), usb.c:1422 the service:
        EP4 TxPktRdy clear? -> uac_packet (usb.c:1287): 44/45(+-1) frames from the ring -> ep4tx -> DMA, TxPktRdy
```

Descriptors: usb.c:335 input terminal (UAC_NCH channels, wChannelConfig 0x0003: L R, the six others non-spatial),
the Type I format (UAC_NCH channels, 16 bit, 44100), EP4 wMaxPacketSize 736, bcdDevice usb.c:375.

## 4. Risks and limits

- **Not yet run on hardware** (2026-11-06): everything above is built, host-tested (render_test "usb stems", ui_test
  USB MIX) and matches the two shipped implementations, but the 736 B packet timing, the 10 kHz service's CPU share
  and host compatibility (macOS, Windows, iOS) need the unit. CONFIG → LOAD's USB AUDIO line reports the measured
  service share and glitches for exactly this.
- **Host naming:** UAC1 can't name channels portably; hosts show 1-8 (L R for 1-2 on some).
- **Windows** caches a device by VID/PID/bcdDevice: 3.38 makes it a new device. Elixir still uses the pid.codes test
  id 1209:0001.
- **Saves to flash** stall the CPU ~17 ms an erase: the ring runs dry and the recording holds (repeated frames).
  Saves happen when stopped, so a recording of a playing pattern isn't affected.
- **The DAW starting or stopping** switches OUT voices in or out of the mix at a half's start (no fade).
