# 8-channel USB audio: memory roadmap (Phase 8, 2026-11-06)

Deliverable 6 of the PRD. **Not needed for 1.0**: the feature fits with 19.4 KB flash and 25.2 KB SRAM to spare
(usb-audio8-resource-budget.md). These are the options if room runs short later, ranked by saving for the risk.

| # | Change | Saves | Complexity | Audio risk | Kind |
|---|---|---:|---|---|---|
| 1 | Phase 9's lock pool (TO_DO_2026-11-06.md, corrected table) | 7.8–39 KB SRAM | high (tear-safe pool) | none | general |
| 2 | Ring to 2 halves + 1 block (320 frames, the band 96..144 still fits: 144 + 128 + 46 < 320) | 1,024 B pool | low | latency unchanged; less margin for a late render | multichannel |
| 3 | `ep4tx` dropped: point EP4's DMA straight at the ring (needs a packet never to straddle the ring's end: pad the ring by 46 frames and copy the wrapped part, or align packets) | ~600 B RAM | medium | a packet must be stable while the DMA reads it: the producer writes ahead only | multichannel |
| 4 | 6 channels instead of 8 (mix + four stems: drums, SN, CY+HH) | 1,536 B pool, 184 B RAM | low | fewer stems | multichannel compromise |
| 5 | `LXR_TRANS` at 8 bits (TECHNICAL_DOCUMENTATION §11.1) | 13 KB flash | low | transients' noise floor | general |

Not worth it: the stem and mix writes have no buffers of their own to remove, and the state (~120 B) is already
small. `ELIXIR_USB_AUDIO=0` gives all of it back at once (RAM identical to before; +484 B flash for the CONFIG page).
