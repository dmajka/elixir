# PRD: Reusable 8-Channel USB Audio for M-VAVE FM-1 Applications

**Status:** Investigation-first / conditional implementation  
**Target:** Elixir and other FM-1/Felucca-derived firmware applications  
**Primary deliverable:** Evidence-based feasibility and resource report, plus an optional reusable USB audio module if feasible  
**Priority:** No regression to existing stereo audio; no assumption that current FM-1 has sufficient free RAM

## 1. Objective

Investigate FNTSMG-1's experimental 8-channel USB audio implementation and determine whether it can be adapted into a small, reusable, application-independent module for Elixir and other FM-1 applications. The desired USB capture configuration is **8 mono channels total: stereo main mix (USB 1–2) plus six discrete mono voice outputs (USB 3–8)** at 44.1 kHz, subject to hardware/driver verification.

The existing Elixir build is RAM-constrained. The project must first **measure actual incremental RAM, flash, CPU, USB bandwidth, and latency costs** rather than presume that 8-channel audio can ship now. It is acceptable—and desirable—to finish with a well-tested, build-disabled module and a clear memory-reclamation roadmap, ready for future refactoring or more capable FM-1 hardware.

## 2. Source evidence and limitations

The provided `fntsmg-1-main.zip` contains FNTSMG-1 application code but **does not contain the actual Felucca submodule contents**. Its `.gitmodules` points to `https://github.com/fantasmogenesis/Felucca.git`, branch `groovebox`. `firmware/build.sh` enables the multitrack build with `FELUCCA_FG=1 FELUCCA_SLICE=0 FELUCCA_UAC_MT=1 ../build.sh`, outputting `fntsmg-1-8pistas.fwsc`; its comment describes **eight pre-mix tracks at 44.1 kHz**. `motores/src/fg_box.h` declares `int32_t trk[8][FG_BLOCK]`, `outl[FG_BLOCK]`, and `outr[FG_BLOCK]`, illustrating distinct track and master buffers. **Do not mistake these application-level buffers for proof of the USB packet implementation.**

The agent must fetch/inspect the exact compatible Felucca submodule commit or branch and the SDK/USB driver before asserting endpoint type, sample packing, DMA behavior, USB class version, buffer size, or RAM overhead. Record commit hashes and licenses. Treat the estimates below as hypotheses only.

## 3. Elixir requirements and routing

Elixir has **six sound voices labeled D1, D2, D3, SN, CY, HH**, and **seven sequencer tracks**. Do not assume a one-to-one mapping between sequencer tracks and independent audio voice outputs. Investigate how the seventh track triggers, shares, or routes sound, and document the actual voice topology before changing audio routing. In particular, verify any shared voice/choke behavior and ensure the stems reflect the six **audio voices**, not an invented seventh output.

| USB channel | Required content |
|---|---|
| 1 | Main stereo mix, left |
| 2 | Main stereo mix, right |
| 3 | D1 mono |
| 4 | D2 mono |
| 5 | D3 mono |
| 6 | SN mono |
| 7 | CY mono |
| 8 | HH mono |

Each of the six voices must have an **independent `Include in Main Mix` toggle**, exposed on the existing **CONFIG page** (with additional CONFIG subpage if needed). Default **ON** for every voice, preserving current stereo playback. Turning a voice OFF removes that voice from the stereo master **but does not mute its dedicated USB stem**. All six stems continue to stream independently of these toggles. State must be reflected in the CONFIG UI and persisted using the project's existing configuration mechanism if feasible; otherwise document why not and propose the smallest compatible migration.

Suggested UI wording: `USB STEMS` / `MAIN MIX` with D1, D2, D3, SN, CY, HH as selectable entries and `IN` / `OUT` values. Follow existing four-encoder/keypad interaction conventions; avoid adding controls that cannot be operated on FM-1. Make it obvious that the toggle controls inclusion in the stereo master, **not** whether the voice is audible on USB 3–8. The toggle should not silently mute the physical stereo output unless physical output is sourced from that same master; document physical-output behavior explicitly.

**Important:** The exact position of stem taps relative to voice gain, pan, envelopes, insert FX, sends, compressor, and global reverb/delay must be determined from Elixir's real DSP graph. Preferred initial design: individual voice audio after voice-local processing but before stereo summing and global effects; main stereo output after normal master processing. Avoid accidental double application of global effects. If removal from master also removes the voice's contribution to global FX sends, state and test that behavior; if sends can remain independently, specify and expose it only after confirming the signal path. Never promise that the stereo master equals the physical output until verified.

## 4. Architecture requirements

- Separate **USB transport**, **sample format/packing**, **capture routing**, and **application DSP adapter** into clear interfaces. No FNTSMG-1-specific synth or sequencer code in the reusable transport module.
- Build-time option, default OFF: retain existing stereo USB firmware and memory footprint when disabled. Consider compile-time choice `USB_AUDIO_MODE=stereo|eight_channel`, adapted to actual build system.
- Fixed 8-channel host enumeration when enabled; no dynamic channel-count changes as users toggle inclusion.
- Keep six stem channels independent of main-mix inclusion. Maintain deterministic channel order and documented labels, with host/DAW channel naming limitations acknowledged.
- Avoid allocating redundant 32-bit full-block buffers where existing voice render buffers can be tapped safely; explore in-place conversion, block slicing, DMA-capable buffers, and bounded double buffering. Do not compromise audio lifetime or DMA coherency.
- Audio rendering must not block on USB host reads. Handle unplug/replug, host suspend, stalled endpoint, underrun, overrun, and mode transitions gracefully.
- Confirm whether USB audio is 16-bit PCM, 24-bit PCM, or another format; sample-rate support, endpoint maximum packet size, USB full-/high-speed capability, descriptors, host compatibility, and asynchronous/adaptive/synchronous feedback behavior must be verified from code and hardware.
- At 44.1 kHz, 8 channels × 16 bits = **705,600 payload bytes/sec**; 8 × 24 bits = **1,058,400 bytes/sec**. At 16 bits, a 128-frame interleaved block is **2,048 bytes**; a 256-frame block is **4,096 bytes**. These are raw data sizes, not measured additional RAM.
- Preserve existing firmware update, MIDI, storage, and USB composite-device functions; inspect endpoint allocation conflicts and host driver limitations.

## 5. Investigation phases and gates

### Phase 0 — Baseline and source provenance

1. Identify exact Elixir firmware commit, build flags, linker script, chip model, memory regions, firmware image size, and build toolchain.
2. Obtain matching FNTSMG-1 and Felucca `groovebox` source including submodule commit; record hashes, licenses, and modifications to upstream Felucca.
3. Build unmodified Elixir and record baseline flash, `.text`, `.rodata`, `.data`, `.bss`, heap, stacks, available RAM, audio timing, and USB mode. Distinguish linker static RAM from runtime peak heap/stack and DMA reservations.
4. If a hardware profiler or debug logs exist, measure worst-case RAM and CPU during high-load patterns, FX, MIDI, display activity, and USB streaming.

**Gate:** No implementation until the baseline is reproducible and USB source provenance is known. If source cannot be retrieved, produce a blocked investigation report with exact missing files.

### Phase 1 — Reverse-engineer FNTSMG-1 multitrack USB

Trace `FELUCCA_UAC_MT` through build flags, descriptors, interface alternate settings, endpoint setup, transfer callbacks, packet scheduler, audio callback, conversion, and interleaving. Identify every changed file and the minimal patch relative to normal Felucca stereo USB. Answer:

- Is the host seeing one eight-channel capture stream, multiple stereo interfaces, or something else?
- Which USB Audio Class version and endpoint mode? How are fractional 44.1-kHz USB frames scheduled (44/45 samples per millisecond or another scheme)?
- What buffers exist, where are they allocated, and are they static, stack, heap, or DMA memory?
- How is audio clock drift managed? What happens if host USB service lags?
- Does FNTSMG-1 support physical stereo output simultaneously? Are all eight outputs pre-mix?
- What limitations or conditional compilation exist for USB MIDI, mass storage, or other peripherals?

Produce a file/function-level call graph, source citations (paths and line numbers), and an exact code-diff inventory. Do not merely repeat README claims.

### Phase 2 — Elixir signal-flow audit

Map each of the six voice generators to its output buffer and sequencer track relationship. Trace dry audio, per-voice processing, pan/volume, sends, stereo sum, global effects, and physical DAC. Identify the lowest-copy point for each mono stem and the point at which the main-mix inclusion mask can be applied without altering the stem. Determine whether the existing mixer can exclude a voice while retaining sends and expected mute/choke semantics. Produce a routing diagram and specify exactly what `IN` and `OUT` do.

### Phase 3 — Resource budget and feasibility

Build and measure at least these configurations where possible:

A. Existing Elixir stereo baseline.  
B. Elixir with multichannel USB transport/descriptors compiled but no extra stem capture.  
C. Eight-channel capture with six stem taps and stereo master.  
D. Full configuration including six mix-inclusion toggles and CONFIG UI.

For each, report absolute and **incremental** flash, `.text`, `.rodata`, `.data`, `.bss`, heap peak, stack high-water, DMA buffers, total peak RAM, audio CPU utilization, interrupt/USB overhead, worst-case callback duration, underruns, and latency. Include map-file and `size` output, and a reproducible measurement script. Record compiler optimization, linker flags, sample format, block size, USB packet size, and test pattern.

Use this budget table (fill with measurements, not guesses):

| Metric | A baseline | B transport | C stems | D UI | Delta D−A |
|---|---:|---:|---:|---:|---:|
| Flash / firmware image | TBD | TBD | TBD | TBD | TBD |
| `.text` + `.rodata` | TBD | TBD | TBD | TBD | TBD |
| `.data` + `.bss` RAM | TBD | TBD | TBD | TBD | TBD |
| Heap peak | TBD | TBD | TBD | TBD | TBD |
| Stack high-water | TBD | TBD | TBD | TBD | TBD |
| DMA/USB reserved RAM | TBD | TBD | TBD | TBD | TBD |
| **Total worst-case RAM** | TBD | TBD | TBD | TBD | TBD |
| Audio CPU worst case | TBD | TBD | TBD | TBD | TBD |
| Underruns over test period | TBD | TBD | TBD | TBD | TBD |

Explicitly report **available headroom and safety margin**, not only theoretical memory use. If runtime peak cannot be measured, label estimates and state the evidence gap. Analyze 16-bit versus 24-bit costs and block-size/buffering alternatives. Establish a clear **ship now / build-disabled only / blocked** decision based on measured RAM margin and stability.

### Phase 4 — Reusable module design (no mandatory integration)

Design a portable library with a narrow interface along these lines (illustrative, not a mandated API):

```c
usb_audio8_init(config);
usb_audio8_submit(master_l, master_r, voice_mono[6], frames);
usb_audio8_set_main_mix_mask(uint8_t six_voice_mask);
usb_audio8_get_stats(&stats);
```

Keep routing/mixing ownership clear: ideally the DSP adapter applies the main-mix mask before summing, while the USB module only receives immutable audio views and packs/transports them. Avoid an API that pretends the USB module can retroactively remove a voice from an already-rendered stereo mix. Design interfaces to be compatible with applications that have different voice counts or track arrangements, while keeping the FM-1 eight-channel configuration explicit. Include compile-time guards, versioning, example integration, and unit tests for interleaving, clipping, channel ordering, and mask behavior. Prefer no heap allocation in audio callbacks.

### Phase 5 — Optional implementation and CONFIG UI

Only if resource and safety gates permit, implement a prototype behind a disabled-by-default flag. Preserve normal stereo firmware as a fallback. Add CONFIG page controls for the six voice main-mix toggles, persistence, and migration/default behavior. Validate stereo master L/R and six individual outputs in a DAW. Do not change existing voice mute/solo controls or sequencer behavior unintentionally.

### Phase 6 — Optimization and future hardware readiness

If RAM is insufficient, **do not force the feature into the production build**. Produce a ranked memory-reclamation plan with bytes saved, complexity, audio risk, and test coverage for each proposal. Investigate buffer lifetime reuse, reducing duplicate track buffers, avoiding redundant interleaved staging, shorter DMA buffers, configurable block size, reducing display/UI memory, and optional features—but only where measurements show meaningful savings. Preserve sound quality and timing. Specify which changes are general optimizations versus multichannel-specific compromises.

Keep hardware assumptions isolated: document what must change for a future higher-RAM FM-1 variant (memory map, SDK, USB controller, DMA alignment, build targets). Do not assume such hardware exists or its specifications are known.

## 6. Acceptance tests

- Host enumerates eight **capture** channels at the supported sample rate, with channels 1–2 as stereo master and 3–8 in the exact D1/D2/D3/SN/CY/HH order.
- Each voice generates audio only on its expected stem (except documented shared-source behavior), verified with isolated test tones or triggers.
- Each CONFIG `MAIN MIX IN/OUT` toggle changes only the selected voice's contribution to stereo master; its dedicated USB stem continues playing.
- With all toggles ON, master output matches the pre-change reference within documented signal-path tolerances; with a voice OFF, no unintended changes occur to other stems.
- Effects/sends, mute/choke, velocity, morph, performance modulation, and any seventh sequencer track are tested explicitly.
- USB streaming remains stable across sustained high-CPU patterns, host reconnect, and DAW start/stop; record test duration and observed underruns.
- Existing stereo-only build has no unintended RAM/flash regression when eight-channel mode is disabled.
- USB MIDI and any composite functions still work, or incompatibilities are clearly identified before release.
- Measured resource table, source trace, and build/test instructions are complete even if the feature cannot ship on present hardware.

## 7. Required agent deliverables

1. `docs/usb-audio8-investigation.md` — source-provenance, USB implementation trace, descriptor/endpoint details, diagrams, risks, and exact file/line references.
2. `docs/usb-audio8-resource-budget.md` — baseline and variant measurements, reproducible commands, map-file analysis, RAM headroom, CPU/latency results, and feasibility verdict.
3. `docs/usb-audio8-routing.md` — six-voice/seven-track topology, signal-flow and FX semantics, channel map, CONFIG behavior, persistence plan.
4. `docs/usb-audio8-module-design.md` — reusable module API, ownership boundaries, integration example, future-hardware considerations.
5. Optional, gated: build-disabled reusable module, Elixir adapter, CONFIG UI changes, tests, and README.
6. `docs/usb-audio8-memory-roadmap.md` — ranked optimizations with quantified savings and risks if current hardware cannot accommodate the feature.

## 8. Agent execution instructions

**Start with investigation and measurement, not implementation.** Inspect the local repository and the provided FNTSMG-1 ZIP; retrieve the missing Felucca submodule if access is available. Never silently invent or approximate source details. Record commit hashes and verify licenses before copying code. Do not modify unrelated DSP, synthesis, sequencer, or storage behavior. Present the Phase 1–3 findings and a go/no-go recommendation before implementing Phase 4–5, unless explicitly authorized to proceed. If there is insufficient RAM, finish the reusable design and measurement deliverables and leave eight-channel mode disabled. Favor small, testable patches and retain an easy stereo rollback.

## 9. Initial sizing hypotheses (NOT measured)

For eight 16-bit channels at 44.1 kHz, USB audio payload is 705,600 bytes/s. One 128-frame interleaved buffer occupies 2,048 bytes; two such buffers occupy 4,096 bytes. The incremental firmware cost depends heavily on the existing stereo USB stack, DMA and callback design, and whether voice buffers already exist. Earlier rough planning estimates of **3–12 KiB incremental RAM** and **2–10 KiB flash** must be treated strictly as unverified and must not be used as acceptance criteria. Actual measurements and peak headroom determine feasibility.
