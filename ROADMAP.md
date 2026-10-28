# Elixir roadmap

## Where things stand (October 2026)

Elixir (called Kettle until 0.13) is the Erica Synths LXR-02 firmware (GPL-3.0) ported to the M-VAVE FM-1 on Felucca's hardware layer. See README.md for controls, building and installing.

**Done**
- **Sound engine:** the LXR's six voices on seven tracks, rewritten in fixed point because the FM-1 has no FPU. `tests/compare_test.c` checks them against the original LXR DSP: levels within 0.1 dB, tonal voices correlate 0.99–1.00.
- **The rest of the LXR:** FX bus (drive, ring mod, compressor, delay), LFOs, mod and velocity routing, kits and morph.
- **Sequencer:** 32 patterns × 4 bars (64 until 0.2), p-locks (two per step), flam and shift, rolls, probability, Euclid, songs, recording.
- **Sends and reverb (0.2):** each voice has an FX send and a reverb send; Felucca's reverb (room, spring, hall) is a send bus after the FX bus.
- **Sound shaping (0.3):** per voice a waveshaper choice, bits, tilt EQ, random per hit, choke group and cross modulation; PHASER and FLANGER on the FX bus.
- **Performance mode (0.4):** PERF's keys: rolls, FILL, repeat, freeze, throws, feedback push, tape stop, filter sweeps (latchable); a DJ filter on the master; four macros.
- **Generative sequencing (0.5):** per step trig conditions and ratchets; per track clocks, generators (mutate, Turing machine, cellular automata, Grids' drum map), dice, commit and undo; COPY + bar copies every track's bar on the grid.
- **Phase 5 (0.6):** bouncing ratchets and the roll sweep, links between tracks, PERFORM 2 (a second bank of PERF effects), physical-model voices for Drum 1–3. CPU on hardware still to be measured. See *Phase 5*.
- **0.7 (Phases 6–8):**
  - The physical models' levels fixed (no more overdrive).
  - The library: backup, restore and LXR-02 kit import over USB, on the site's `library/` page and in the emulator.
  - A private-build kit bank (`ELIXIR_KITS_DIR`).
  - The code is at https://github.com/dmajka/elixir.
- **0.8 (Phase 9, the LXR's live feel):** PERF's pages reordered (the performance pages, MIX, then the macros, then PERFORM 2), the LXR's PERFORMANCE page with its global sample rate (SRate), MIX pages for the voices' volumes; KEYS (SHIFT + EDIT) to play a voice, with a key velocity and accent; LXR project import (kits, patterns, songs); a kit library in flash (128 kits, 128 sounds) with SOUND save and load and morph from it; a per-track sound browser. See *Phase 9*.
- **0.9 (Phase 10):** the physical models replaced by a fixed-point port of Mutable Instruments Rings' modal resonator, string and FM voice, checked against Rings' own code. CPU on hardware still to be measured. See *Phase 10*.
- **0.11 (Phase 13):** Control All: OCT+ held on a sound page, a knob moves that parameter on every track, relative; letting go asks: OCT+ reverts, OCT− keeps. See *Phase 13*.
- **0.12 (Phase 12, 12a–12e):** the remote map for an 8-column controller (Launch Control XL): CONFIG → REMOTE channel and knob mode, knob rows on CCs 8–119, buttons on notes 36–93, feedback over USB; NRPN morph and roll rate fixed. The controller templates (12f) are still to make. See *Phase 12*.
- **0.14 (Phase 15):** renamed Elixir (Kettle until 0.13); a pixel-potion title screen; the install and library pages find the FM-1 under either name, so Kettle units update in place (`tests/install_test.mjs`); a new front page for the site. See *Phase 15*.
- **0.15 (Phases 16 and 17a–17c, released 2026-10-27):** Plaits' sixteen original engines as models on D1–D3 (experimental, as Rings'); the second core renders D1–D3 (stock kits ~27% CPU on core 0, was ~45%), guarded, with a one-core fallback; at most two models at once (one on one core); CONFIG → LOAD, the load by part and the cores; morph refreshed as a sweep; Plaits' speech words found at boot. Tested on the unit through 0.15-test1 to test4. The manual says the models are experimental and not in the LXR-02. See *Phase 16*, *Phase 17*.
- **0.16-test1 (Phases 18–23, built 2026-10-27, not yet tried on the unit):** groundwork (the effects a block behind on core 0, LIB_MAX), a scale per pattern that the tracks follow, note lengths and GATE, a third global LFO, DENSITY on PERF, a melody generator (after Marbles), a tape/dub delay (TAPE, TAPE PP, SYNC, ECHO > REVERB, GLITCH, PITCH), Clouds' plate as the reverb's fourth model, dirt (four more shapes, the bus's drive, a master DIRT). See *Phases 18–23*.
- **0.16-test2 (built 2026-10-27, after the first try of test1):** the FX pages in order (REVERB, FX BUS, the type's parameters, DELAY: DUB and DELAY: SEND only on DELAY, DIRT); the remote map reworked onto two channels, TRACKS (15) and GLOBAL (16), every number used once per channel (mute and solo had shared CCs with knobs), buttons as CCs or notes, feedback the way a button last arrived, DENSITY's amount and track toggles added, random decay dropped, 0.15 settings migrated; the HYRULE palette (appended: saved ids stay); the site's LFO count fixed (three).
- **0.14.1:** LFO MODE moved to the page right after the LFO's two pages (it was the last sound page); SELECT follows a fixed section order.
- **0.13 (Phase 14):** the LFO's modes (TRIG, HOLD, ONE, HALF); two global LFOs on morph, the DJ filter and PERF's effects, down to 256 bars a cycle; CONFIG → BUTTONS, Last page; Euclid's second pulse (OR, XOR, AND, SUB). See *Phase 14*.
- **MIDI:** USB and TRS in, USB out. CC/NRPN control as on the LXR; clock out always on; clock in by turning the tempo below its minimum to "MIDI".
- **Projects:** 4 slots plus an AUTO session slot in flash, 48 KB each, sparse encoding. Format version 7 since 0.9; versions 1 to 6 still load (6's models mapped to Rings').
- **UI:** mapped to the FM-1's keys, buttons and knobs; SHIFT legend on screen; mixer under GLO.
- **Firmware build:** `./build.sh` makes `build/elixir.fwsc` and passes Felucca's checks.
- **Browser emulator:** `web/emu/build.sh`, tested in Chrome, with Web MIDI in and out, including clock.
- **First hardware run (0.5.1):** installed on a real FM-1 from V15 with the install page (`web/make_site.py`); a battery display on CONFIG; autosave no longer makes a burst of noise after a stop. See *0.5.1* below.
- **Tests:** render_test, compare_test, library_test (the library page's code against the device's), install_test (the install page's updater against simulated FM-1s, Kettle to Elixir), rings_compare (the models against Rings' float code) and ui_test (96 screens; CONFIG → LOAD and the cores, the models' limit; Phase 14 (LFO modes, global LFOs, Last page, Euclid 2); the remote map; Control All; Rings' models; PERF's pages, MIX and KEYS; the kit library and sound browser; PERFORM 2; links between tracks; bouncing ratchets and the roll sweep; the output's hush and autosave's timing; the battery display; Grids' map; clocks, conditions, ratchets, generators, bar copy; locks, patterns, project save/load, a 0.1 project, sends, reverb, sound shaping, phaser, flanger, PERF's effects and macros, autosave failure, sync in) all pass.

**Measured resources** (from the 0.13 build, 2026-10-26; build.py's numbers. The build fails below 4 KB of RAM or 8 KB of pool spare, so "usable" is the free less that.)

| | Used | Total | Free | Usable |
|---|---|---|---|---|
| Fast RAM (.data + .bss) | 42,688 B | 73,728 B | 31,040 B | ~27 KB |
| Pool (big buffers, same SRAM) | 347,268 B | 368,640 B | 21,372 B | ~13 KB |
| App flash (image) | 331,904 B | 581,564 B | 249,660 B | ~244 KB |
| Project slot | busy test project 35.8 KB | 48,896 B payload | ~13 KB | dense projects already fail (PROJECT TOO BIG) |
| Kit library (flash) | 72 KB at 0xEA000–0xFBFFF | | 128 kits + 128 sounds | a kit entry holds 480 B (kit 377 B) |
| Library SysEx message | a kit: 390 B | LIB_MAX 400 B | 10 B | (static assert; raising it costs ~3.3 B of RAM a byte) |

Where it goes: RAM: Rings' lines 18 KB (`rg_line` 12 KB, `rg_stretch` 6 KB), `mdl` 3.4 KB, `sq` 2.3 KB, `vc` 2.1 KB, the audio buffer 2 KB. Pool: the 32 patterns 159 KB (5,099 B each), the FX bus's line 64 KB, the screen canvas 30 KB, the 64 kits 24 KB, REVERSE's recorder 11 KB, the reverb's combs 10 KB, the comb 8 KB, and three spare patterns (live generators, backup, undo) 15 KB. Phase 14 cost +292 B RAM, +1,168 B pool, +4.5 KB flash, 6 bytes a kit.

Rules of thumb: a per-step byte costs 32 × 7 × 64 = 14 KB of pool (more than is usable); a per-track byte 224 B; a per-pattern byte 32 B; a per-voice kit parameter 6 B a kit (384 B of pool) and brings the library's message closer to LIB_MAX.

**CPU on a real FM-1 (0.5, first measurement):** about 60% average and 64% peak with a busy pattern. The 122 LATE buffers seen then were not the engine's load (a 64% peak leaves a third of each buffer spare) but autosave's flash erases, which hold the processor; 0.5.1 no longer counts those. The load is on screen (bottom of the CONFIG pages: average, peak since CONFIG opened, late buffers since power-on). The browser emulator always shows 0%, because it doesn't model CPU time. From 0.15-test2 the second core renders D1–D3 (Phase 17c); the load by part is on CONFIG → LOAD (17a).

**Rules of thumb**
- Per-track settings cost almost nothing. Per-step data multiplies: 64 patterns × 7 tracks × 64 steps means 1 bit per step ≈ 3.5 KB and 1 byte ≈ 28 KB. At 32 patterns, halve those.
- No floats, and no 64÷64 division on the target. Divisors must be 32-bit, or the link fails on `__udivdi3`/`__divdi3`. Felucca links no runtime library.
- Avoid constants between 0xFFC00000 and 0xFFCFFFFF (for example −4194304). build.py's ROM-address guard rejects them.
- After each change: `./build.sh`, the three host tests, and `web/emu/build.sh` for a browser check.

## Phase 1: headroom, sends, reverb (done in 0.2, except the CPU measurement on hardware)

What was built, and where it differs from the plan:
- **Projects** keep six 48 KB slots (four user slots plus AUTO), so 0.1 projects stay readable where they are. Version 2 stores a step as the fields that differ from an empty step. A step with velocity and note set takes 3 bytes instead of 8. In `ui_test`, a busy project (32 patterns, every track 32 steps, velocity and note on each step, every kit edited) takes 31 KB. A fully dense project (64 steps, every field, every kit parameter) still reports PROJECT TOO BIG. Fewer, larger slots are still an option.
- **AUTO save** used to fail silently when the session was too big, then retry every ~10 s, erasing 48 KB of flash each time. Now it shows AUTOSAVE: PROJECT TOO BIG for 6 s and does not retry until the session changes. A related bug that was already in 0.1 is fixed: saving a project that is too big wrote past a 256-byte buffer.
- **Elixir's new parameters** are numbered 302–317 (NRPN 174–189) and are stored in the kit after the LXR's sound parameters, so they morph and save with it. A step stores a lock as one byte, so locks on them use lock numbers the LXR leaves free (247–254 and its UNUSED 221–227). The reverb model can't be locked.
- **FX send** is a split, not an extra send: it sets how much of the voice goes through the bus, because the LXR's bus returns dry plus wet. At 127 it is the old Out = FX BUS (`ui_test` checks they are sample-identical). FX BUS is no longer an output choice.
- **Reverb**: ROOM's feedback now rounds toward zero. Felucca's ROOM leaves a DC offset of about −33 after each hit, which Felucca's master DC blocker hides and Elixir has none of. The reverb sleeps when it isn't fed and its tail is silent.

The plan as agreed:

1. **Cut to 32 patterns** (`NUM_PATTERN` in seq.c). Frees about 115 KB of pool.
   - Pattern page: 2 banks of 16 instead of 4.
   - Program change: only 0–31 select patterns.
   - Songs: clamp references above 31.
   - Projects: format version 2. Version-1 projects load their first 32 patterns.
   - Projects also get more room: consider fewer, larger project slots, since dense projects can hit the 48 KB "PROJECT TOO BIG" limit. Check whether the AUTO save reports that failure or skips it silently.
2. **CPU meter on screen** (CONFIG page or header): show `cpu_q8`. Install on an FM-1 and record the load with all voices and FX busy before adding DSP.
3. **Per-voice send levels**, replacing the all-or-nothing Out = FX BUS.
   - Each voice gets an FX-bus send and a reverb send, lockable.
   - Mixer: add **MIXER: SEND** pages. Keep old kits working: Out = FX BUS becomes send 100%.
4. **Felucca's reverb** (`Felucca/firmware/src/fx.c`: ROOM, SPRING and HALL in about 12 KB of shared buffers) as its own send bus after the FX bus.
   - Parameters: model, size, damping, level; plus the return level on the mixer's free pan-page knobs.
   - New parameters go after the LXR's numbering and are reached by NRPN.
   - Fade the old model out when switching, as Felucca does.

## Phase 2: sound shaping (per voice, lockable) (done in 0.3, except the optional resonator)

What was built, and where it differs from the plan:
- **Parameters**: 66 new ones (318–383, NRPN 190–255), 11 settings of six, all per voice. Random per hit is per voice, not per track: the open and closed hats share theirs, as they share every other voice setting.
- **Locks**: 0.2's trick of reusing free lock numbers had run out. Each step now has one more byte holding the high bits of both locks' numbers (`lhi` in seq.c, ~15 KB for 32 patterns), so any of Elixir's parameters can be locked except the reverb model. Projects are version 3. Version-2 projects load, and their locks on the sends and reverb move to the new numbers.
- **Kits** moved from fast RAM to the pool (21 KB): the audio ISR never reads them.
- **Waveshaper**: the LXR's curve stays the default and sounds exactly as before. The others take the voice's own DRIVE as a 1×–8× gain.
- **Choke**: a group's hit fades its other voices out over one block (1.5 ms). They stay silent until their next hit.
- **Cross mod**: DUCK and RING use the source's current block (all voices render before the mixer now). FILTER uses the source's level from the previous block, because the filter runs while the voice renders.
- **Phaser**: 6 allpass stages per channel with feedback. **Flanger**: uses the FX bus's delay buffer (2048 samples per channel). Switching the bus to FLANGER or DELAY clears that buffer.
- Not built: the optional tuned resonator / comb.

The plan as agreed:

Felucca's per-track insert (`fx.c`, CRUSH) is a reference for bit reduction.

- **Bit-depth reduction** per voice, alongside the existing sample-rate reduction: a full bitcrusher.
- **Waveshaper choice** per voice: fold, hard clip, asymmetric, rectify.
- **Random per hit** (Perkons-style): per-track amounts of pitch, decay, filter and pan variation on each trigger.
- **Tilt EQ** per voice (one-pole).
- **Choke groups**: a hit cuts or retriggers another voice.
- **Cross-modulation** (Pulsar-style): one voice's envelope or output modulates another, for ducking, filter opening and ring mod.
- **Phaser** as a new FX-bus type (all-pass chain plus LFO; no memory). **Flanger** using the delay buffer.
- Optional: a **tuned resonator / comb** per voice (Karplus-style), a few KB per voice that uses it.

## Phase 3: performance mode (PERF on ARP) (done in 0.4)

What was built, and where it differs from the plan:
- **Keys:** 1–7 roll their track; 8 is FILL (key 8 isn't a roll: there are 7 tracks); 9–16 are repeat, freeze, throw reverb, throw FX, feedback push, tape stop, LP sweep, HP sweep. SHIFT + key latches. In PERF, SHIFT + white keys no longer reach the LXR's SHIFT functions (kit, song, project ...); leave PERF to use them. Leaving PERF turns latches off. The bar keys pick the repeat length.
- **Repeat** loops the sequencer, not audio. It starts on the next point of its grid and resumes on time (the ticks it held back are stepped through silently). Pattern and song changes wait while it loops.
- **Freeze** is the FX bus delay looping, as planned. It needs the bus set to DELAY (the screen says so otherwise). Its tone filter is bypassed while frozen, so the loop keeps its brightness.
- **Throw** acts on the selected track's voice (the ALGORITHM encoder selects it; in PERF the black keys are mutes). There are two keys: reverb send and FX send.
- **FILL** plays every step whatever its probability. Phase 4's FILL trig conditions will use the same key.
- **Tape stop** slows the voices' pitch and the internal clock (and MIDI clock out with it); the master fades out over the last quarter of the speed.
- **Macros** are saved per project, not per kit: a macro often spans voices or the FX bus. The settings go at the end of the project, after the songs (format version 3 still: 0.3 reads up to the songs and ignores the rest). Macro targets are the lock targets.

The plan as agreed:

- **White keys 1–8:** rolls/repeats (as now). **White keys 9–16:** momentary effects. SHIFT latches. **Black keys:** mutes.
- **Stutter / beat repeat:**
  - Sequencer-based: loop the play position at 1/4 to 1/32 and re-trigger the hits. No memory.
  - Audio freeze: the delay buffer as a loop (input off, feedback 100%). About 0.74 s, so 1/4 note above ~80 BPM.
- **DJ filter** on the master: one knob, low-pass left of centre and high-pass right; reuses the existing filter code.
- **Dub throw:** while a key is held, a voice's send jumps to 100% and the tail rings out on release. Also a **feedback push** key (momentary ~95%).
- **Tape stop:** ramp the tempo and every voice's pitch down together. No audio buffer needed.
- **Fill** key and momentary filter sweeps.
- **Four assignable performance knobs:** any parameter with a min/max range, saved per kit/project. Knob moves under REC are recorded as locks (already supported).

## Phase 4: generative sequencing and step extras (done in 0.5)

What was built, and where it differs from the plan:
- **Bar copy** (asked for alongside Phase 4): COPY + bar key on the grid copies the shown bar of every track, with all of each step, into that bar. Tracks whose length is whole bars grow to include it; other pages still copy only the selected track.
- **Step data**: two new bytes a step (condition; ratchet hits, rate and fade), not packed into spare bits, because there weren't enough. With the track settings, a pattern grows from 4.1 KB to 5.0 KB (about 40 KB in all with its copies). Project format version 4.
- **Ratchets** have their own rate (1/32, 1/48, 1/64, 1/24) and can run past the step. The LXR's flams already cover even repeats inside a step. Velocity fade: flat, down, up.
- **Conditions**: A:B (B up to 8), FILL / !FILL (PERF's key 8), PRE / !PRE, NEI / !NEI (the previous track's last condition), 1ST / !1ST. Probability still applies separately.
- **Track clocks** are per pattern track (/4 … x4, with 2/3, 3/4, 4/3, 3/2). The sequencer now runs each track on its own clock. The pattern's end is its longest track's length in ticks. Flams and shifts scale with the track's step.
- **Generators** play a live copy of the track and never change the pattern itself until COMMIT. UNDO is one level, per track, and swaps (so UNDO again redoes). Stopping and starting keeps the live copies; a pattern change, a project load, RESET or turning a generator on copies the pattern again. Turing's register is per track. Mutate makes up to two changes per time round.
- **Dice** places the Euclid page's number of hits at random over its length.
- **GRIDS** (the optional map, added once its licence was checked: Grids' AVR code is GPL-3.0-or-later, compatible with Elixir's GPL-3.0-only). It is a fourth generator: a track takes a part (BD, SD, HH, defaulting from the track) with its own density and accent. The map's X, Y and chaos are the pattern's (the GRIDS MAP page), as on the module, so moving them moves every Grids track together. The 25 nodes come from Grids' `resources.cc` via `tools/gen_grids.py` (output committed as `firmware/src/grids_map.h`, 2.4 KB of flash). The map is read exactly as Grids reads it (checked at 2,700 points). Grids' bar is 32 32nd steps: a Elixir step reads its two, and a hit only on the second plays half a step late, using the step's shift. Chaos perturbs the levels once each time round, as Grids does. Project format version 5 stores the map.

The plan as agreed:

**Per track (free)**
- **Clock division/multiplication** (/2, ×2, 3/4 and so on) for polyrhythms.
- **Mutate:** each loop, with probability X, flip or move a step, or drift a locked value. With **freeze** and **commit to pattern**, plus undo.
- **Turing Machine** shift register per track, with lock, length and density.
- **Dice:** regenerate a track within its density and Euclid settings, with undo.
- **Cellular automata** (rule 30, 90, ...) as a track generator.
- Optional: **Grids-style** topographic drum map (done: the GRIDS generator).

**Per step** (needs the Phase 1 headroom; add both in one pattern-format change)
- **Ratchets:** 2/3/4 hits in a step, with their own rate, settable per step.
- **Trig conditions** (Elektron-style): A:B cycles, FILL, PRE/NEI, first/not first.
- Possibly pack both into spare bits of the existing step data, such as the flam field.

## 0.5.1: the first hardware run

**Installing.** The install site (`web/make_site.py` → `build/site`: the install page, the emulator under `try/`, the manual as a page with a contents side bar, the package, source and licences under `firmware/`). Found on the way:
- Elixir's USB name ("Elixir") wasn't in the updaters' port filter (`web/fm1ota.js`, `tools/fm1_install.py`), so neither could have found an FM-1 running Elixir to take it back to V15. Fixed: both match "elixir".
- The manual's recovery advice was wrong: UBOOT (OCT− + OCT+, the boot guard) is the chip's mask-ROM mode, and only the FM-1-transporter can write from it. Rewritten (MANUAL.md section 1).
- An FM-1 running another alternative firmware (Jiant) wasn't found: its USB name isn't one the updater probes. It went back to V15 with its own installer, as the page now advises; "not found" lists the MIDI ports the browser sees.
- On one computer the update loader ("Felucca Update", USB 1209:0002) never appeared after V15 handed over, not even in Audio MIDI Setup; turning the FM-1 off and on returned it to V15 unchanged. On another Mac it appeared within 40 s and the install finished. So: that computer's USB path, not the package (Elixir's loader is byte-identical to Felucca's).
- The install page's progress bar assumed half the write's real number of requests; it now follows the address the loader reads. The log is folded under Details (opened on a failure).

**Battery** (asked for after the first run). Felucca's reading: ADC channel 3 through a slow IIR, the stock thresholds (531 / 561 / 591) for 0..3 bars; USB power shows USB (`master.c` `batt_poll`, `batt_level`). Drawn at the right end of the CPU line on every CONFIG page (`ui_draw.c` `draw_batt`).

**Noise after stopping** (reported: a short burst like white noise after stopping a long play; gone with AUTOSAVE off). Two causes:
- A flash erase holds the CPU ~17 ms. Felucca's storage silences the audio buffer for each erase, but a project save erases 12 sectors, and the reverb and delay tails played in fragments between them: the burst. It also counted as LATE.
- Autosave's 8 s rest counted from the last save, not from the stop, so after a long play the save came within 2 s of pressing stop, on top of the tail.

Fixed: every save (`storage.c` `st_save_to`, `project.c` `proj_save`) fades the output out first (~6 ms, the audio ISR's ramp: `engine.c` `out_hush`), writes, and fades back in; buffers late during a save aren't counted. Autosave's rest starts at the stop and at each change, and it also waits until the output (before MASTER) has been below ~-60 dBFS for half a second (`quiet_blocks`), so the tails have died away and the fade cuts nothing audible; after 30 s at rest it saves anyway (a frozen delay can ring on). ui_test checks the fade, the timing (8.1 s after a stop) and the fallback.

**Discussed, not built:**
- **The second core.** Felucca has it (`FELUCCA_CPU2`, `hal/fm1_cpu1.h`, a menu option, off by default because core 1 misreads RAM on some FM-1s, with a fallback to one core). Moving the reverb or the FX bus there might bring 60% to roughly 35–45%. Not needed at today's load; worth it before adding heavy DSP such as a second FX bus.
- **A second FX send** (two effects at once). The FX bus's delay buffer is 64 KB and the pool has ~49 KB free, so a second bus can't have a full-length delay. Options: (A) bus 2 offers drive, phaser, flanger and a shorter delay (~0.35 s); (B) the two buses split the delay memory. Each voice gets a second send, the mixer another send page; parameters, a project format change and tests: about a phase. The user is thinking about it.

## Phase 5 (built in 0.6, except the CPU measurement on hardware): bouncing ratchets and roll sweep, track coupling, PERFORM 2, physical-model voices

The user's direction (October 2026): experimental, in the spirit of Aphex Twin, Autechre, microhouse (clicks and cuts) and dub techno, and of the Torso S-4's audio effects, but within the FM-1's means. Four parts, built and checked in on one at a time (each: emulator test, then the CPU meter on hardware). In order of size:

| Part | What | CPU (estimate) | Pool memory | Project format |
|---|---|---|---|---|
| 5a | Bouncing-ball ratchets | ~0 | ~0 | none (a spare bit) |
| 5b | Track coupling | ~0 (sequencer) | ~1 KB | version 6 |
| 5c | PERFORM 2: a second bank of performance effects | 1–5% (only while on) | ~16 KB | KPF2 section |
| 5d | Physical-model voices | ~1–3% per voice (measure) | ~7 KB | kit parameters |

Budget today: the pool has ~49 KB free and the CPU averages ~60% (64% peak) on a busy pattern. After this phase ~25 KB would be left. That rules out a second FX bus with a full-length delay (it didn't fit anyway), and the second core stays the reserve if the physical models cost more than estimated.

### 5a. Bouncing-ball ratchets (built)

What was built, and where it differs from the plan:
- **Bounces** (`seq.c` `bounce_start`, `seq_bounces`): RAT_BOUNCE is bit 7 of the step's `rat`. Hits come from 1, 3, 4, 5, 6, 8, 12, 16. Index 0 stays "off", so 10 was dropped rather than losing OFF. The curve sits in the rate bits. Gaps are counted in Q8 samples and run down one block (64 samples) at a time, scaled by TAPE STOP's rate. A gap is never shorter than one block, and SLOW's gaps stop growing at half a bar. Hits land at the start of a block (1.45 ms resolution), which is fine at these speeds. The first hit is the step's own, and the pitch drift reaches its full amount on the last hit. DRUNK uses a xorshift, which 5b's chances will share.
- **Per track:** `bnc[t]` = first gap | (pitch + 12) << 2, on TRACK CLOCK's knobs 3 and 4 (Bounce, Bnc pitch). Project format version 6 stores it after the generator, only when it differs from the default (1/8, no drift). Version 6 also keeps bit 7 of `rat` on load; versions 4 and 5 mask it off.
- **The cut:** a new occurrence of a hit on the track cuts a bounce, but a flam's later hits don't. Stopping clears the bounce and its occurrence, so playing again restarts it. ui_test found that bug: the occurrence is (loop, step), and loop 0 repeats after a stop.
- **Roll sweep** (`seq_roll_sweep`, `roll_period`): while a roll is held or latched, PERFORM's KNOB 3 is Roll, at positions 0..127 from 1/4 to 1/192 on a log scale. That reaches further than planned, into a buzz at 1/192 (4 ms at 120 BPM). It is sample-timed like the bounces, and turning it faster takes effect at once. SHIFT + KNOB 3 is the grid roll rate (PAR_ROLL), which turns the sweep off, as setting it on PERF: MORE does. The swept rate isn't saved. When no roll is held, KNOB 3 is REPEAT again.
- **Screen:** the RATCHET page's KNOB 2 reads Curve and names it (BALL …) when bouncing. Hits shows the bounce count. A card's redraw signature now includes the bounce bit, so turning Shape redraws the other cards.
- Tests (ui_test, now 61 screens): 8 BALL hits at 250, 175, 122, 87, 58, 44, 29 ms gaps (each 0.7 of the one before, the first 1/8 at 120 BPM). SLOW's gaps grow. The drift, the cut by the next step, the RATCHET and TRACK CLOCK pages, the sweep's rate (2 hits/s at the slowest; 23/s at 80, which is 1/46), KNOB 3's change of role and back, and a version 6 round trip.

The plan as agreed:

A ratchet whose hits get closer (a dropped ball) or further apart (slowing down), with the velocity falling as it goes.

- **Per step** (STEP edit, RATCHET page): a fourth knob, **Shape: EVEN / BOUNCE**. It uses the step's spare ratchet bit (bit 7 of `rat`: no format change). With BOUNCE:
  - **Hits** reads from a longer table: 3, 4, 5, 6, 8, 10, 12, 16.
  - **Rate** becomes the **curve**: BALL (each gap 0.7 of the one before), TIGHT (0.55, settles fast), SLOW (each gap 1.3x: a slowdown), DRUNK (0.7 with ±15% random per gap).
  - **Fade** works as now (DOWN is the natural ball).
- **Per track** (TRACK CLOCK page, its free knobs): **Bounce start** (the first gap: 1/4, 1/8, 1/16, 1/32) and **Bounce pitch** (each hit drifts by up to ±12 semitones over the bounce, for falling or rising zaps, Aphex-style).
- **Timing:** the gaps are counted in samples (the sequencer's block, 64 samples = 1.45 ms), not in ticks (5.2 ms at 120 BPM), so a ball's last bounces can get closer than a tick. To check: how the voices take a hit inside a block.
- A bounce may run past its step and the next step (as ratchets can). A new hit on the track cuts it.
- Tests: the hit times against the geometric series, the pitch drift, the cut.
- **Roll sweep** (asked for on review): hold a roll key (PERF 1–7) and turn KNOB 3 to sweep that roll's speed live, from slow to fast and back, without leaving the PERFORM page. Today the roll rate is read live but sits on PERF: MORE and jumps between 14 grid values (`seq.c` `ROLL_TICKS`, fired on `sq.ticks % every`). The sweep uses 5a's sample-timed hits, so the rate moves smoothly (1/4 to 1/64, a little past it into a buzz); SHIFT while turning snaps it to the grid values. KNOB 3 goes back to REPEAT when no roll key is held (the knobs are endless encoders, so nothing jumps). The swept rate stays as the roll rate.

### 5b. Track coupling (built)

What was built, and where it differs from the plan:
- **The sequencer** (`seq.c` `seq_flam`, `seq_links`): `seq_flam` now runs in two passes. First it finds each track's hit for the tick (its step, loop and position), then `seq_links` runs links 1–8 in order, then the hits play. With no links set, or a depth of 0, `seq_links` isn't called and `step_hit` gets no changes, so playback is as before.
- **Each link:** its event arms a window counted in the target's own steps (`sq.nst`, the steps since the start). The target's hit in the window gets the effect. Events and changes are worked out once per occurrence (`sq.locc`, `sq.lmod`), and a flam's later hits keep them. A dropped hit counts as a rest (SILENT) for the links after it. A muted source rests too.
- **Effects:** DUCK and ONLY are chances (`seq_rand`). The plan's "CHANCE+ / ONLY IF" became ONLY: a target's hit outside its window is dropped with the amount's chance. Raising a failed step's chance was left out, because the step's chance is rolled before the links run. TRIG plays the target's current note with no locks, the window serving as its delay. RATCH gives a step with no ratchet of its own +1..+4 hits at 1/32, or a BALL bounce; a step's own ratchet wins. VEL and PITCH change the one hit. DECAY uses a third automation lane (`sound.c` `auto_par[t][2]`) on the track's amp decay, for the hit and its ratchet, and the track's next hit clears it. SHIFT holds the occurrence's first hit in a one-hit slot per track (`sq.dly`, played by `seq_delayed`), and drops a flam's later hits.
- **Amounts** (`LK_LO`, `LK_HI`, `LK_DEF`) are stored as int8 and scaled by the depth (`cdepth`, 0..127).
- **The UI:** two pages after GRIDS MAP. LINKS has Link, Start, Amount and Depth. LINK SETUP has When (track and event as one knob), Target, Effect and Window. The plan's "pressing again" for a second set of knobs became the second page. The body shows the row of 8 links and the shown link as a sentence. Start's four starting points are `LK_START`; it reads EDITED once the link is changed, and – clears it. Choosing an Effect on an empty link makes one from the shown track to the next. SHIFT + SEQ opens LINKS (and the SHIFT legend says so). The grid's labels mark the tracks a link joins.
- **Not done:** **Depth as a macro target.** Macros target the kit's parameters (`par[]`), and the depth is the pattern's. That needs a pattern-level target type; the Depth knob is there instead.
- **Project:** links and depth were already in format 6 (5a's change), written only when set.
- **Tests** (ui_test, 66 screens):
  - DUCK over each window (SAME, NEXT, NEXT 2, BAR: 12, 12, 8, 0 of 16 hits) and at 50%.
  - Depth 0 is as no links (the same hits, and the link code isn't entered). This replaces the planned sample-identical render, since the emulator can't replay from a saved state.
  - ONLY with SILENT, ACCENT, and a loop both ways round.
  - TRIG's delay and velocity, RATCH +2, VEL, PITCH and DECAY on one hit (and the next hit as set), and SHIFT's half step.
  - The pages and SHIFT + SEQ, Start, Amount, Effect and When, the grid mark, and a round trip.

The plan as agreed:

Tracks that react to each other, so a pattern behaves like a small system rather than a loop. Each pattern holds up to **8 links**:

**SOURCE** track **EVENT** → **TARGET** track **EFFECT** by **AMOUNT** within **WINDOW**

- **Events:** HIT, ACCENT (velocity above 100), SILENT (the source's step had nothing, or its chance failed).
- **Effects:**
  - CHANCE− (the target's hit becomes less likely: a kick ducking the hats)
  - CHANCE+ / ONLY IF (the target plays only after the source: call and response)
  - TRIGGER (a sympathetic hit at the amount's velocity)
  - RATCHET (+1 to +4 hits, or a bounce)
  - VELOCITY ±
  - PITCH ± (on that one hit)
  - SHIFT (pushed late by up to half a step: a nervous, Autechre feel)
  - DECAY ± (on that one hit)
- **Windows:** SAME step, NEXT step, NEXT 2 steps, rest of the BAR.
- **Order:** at each tick, every track's would-be hit is decided first (conditions, chance, generators), then the links are applied, then the hits play. So a link can go either way between any two tracks, and loops (A ducks B ducks A) are allowed: each link sees the hits decided before it.
- **COUPLING depth:** one per-pattern amount scaling every link (0 = off), a macro target, so the system can be faded in live.
- **Page:** EUCLID group, a fifth page **LINKS**. SELECT picks the link; KNOB 1 to 4: Source, Event, Target, Effect; pressing again: Amount, Window, Depth. The screen lists the links as sentences ("D1 hit → CH chance −60%, next step").
- **Starting points** (KNOB on an empty link): DUCK HATS, CALL / RESPONSE (OH only after a CH rest), GHOST FILL (D2 triggers softly when D1 is silent), CHAIN (a snare hit ratchets the next hat).
- Pattern data: 8 links × 4 bytes = 32 B a pattern (1 KB for all 32). Project format version 6 (versions 1 to 5 still load, with no links).
- Tests: each effect, the window, a loop, depth 0 = identical to no links (sample-identical render).

### 5c. PERFORM 2: a second bank of performance effects (built)

What was built, and where it differs from the plan:
- **Pages and keys** (`ui.c`): PERF has 7 pages: PERFORM 2 (the same knobs as PERFORM), then PERFORM 2: SETUP and SETUP 2 (VP_P2 .. VP_P2 + 7, i.e. `pfs.drill` .. `pfs.rev_len`). On those three pages the white keys are bank 2 (`perf_bank2`). Each key remembers the bank it went down in (`ui.kbank`) and lets go there. Bank 2's effects are bits 9..17 of `pf.on`, now 32 bits, as are `ui.pf_held` and `ui.pf_latch`. DRILL has its own held and latched masks (`sq.drill`). Leaving PERF lets go of latches in both banks. The key labels are three letters (HLF JIT CUT SHT PMP RES SPC CRS REV); COMB's key is RES because "CMB" doesn't fit a key.
- **The sequencer** (`seq.c`):
  - HALF counts every other tick (`sq.half`), from the internal clock and from MIDI sync. MIDI clock out is unchanged.
  - JITTER is late-only: a random 0..N ticks (N up to 12, a 32nd) per occurrence (`sq.jit`, `sq.jocc`, `sq.jd`). A hit can't be played early without looking ahead, so the plan's "±" became late.
  - Hits held back (JITTER and 5b's SHIFT) now go in a queue of 4 a track (`sq.dq`, `dq_push`, `seq_delayed`), so a flam's later hits are held back too, where 5b dropped them.
  - DRILL makes a random ratchet on each hit, overriding the step's. The ratchet's later hits get random velocity and ±2 semitones. Ratchets now keep their rate, fade and note from when they started (`sq.rper`, `sq.rfade`, `sq.rnote`), which also covers 5b's link ratchets.
- **The voices:**
  - SHORT raises every voice's amp decay rate to `SHORT_DEC` each block while it's on.
  - PUMP: `perf_block` follows D1's hits (`trig_count[0]`). Its envelope rises in 2 blocks and falls over an eighth; the gain is 1 − depth × env². Every voice but D1, sends included, goes through it (`voices.c` `pump_g0` → `pump_g1`, ramped across the block).
  - SPACE isn't a true freeze. Each reverb model's feedback goes near 1 (ROOM 0.99, SPRING 0.98, HALL at SIZE 240, its lines at their 0.99 cap), the reverb doesn't sleep, and the dry mix is dipped to 0.4 before the FX bus and reverb returns join it. The input stays open as planned.
- **The master** (`perform.c` `perf2_master`, after the DJ filter):
  - COMB: a 4096-sample line in the pool (8 KB), a fractional delay for the note, a gentle low-pass in the loop, feedback 0.5–0.98, the input scaled by (1 − fb) × 4 so it rings without blowing up.
  - CUTS: a new random gate every 32nd, with 32-sample fades.
  - CRUSH: sample hold of 1..16 and 16..4 bits.
  - REVERSE: a recorder of 0.25 s at 22 kHz, mono, in the pool (11 KB). While it plays, the recorder stops, the part loops backwards with 32-sample fades at its ends, and it crossfades in and out (64 samples).
  - Pool left: ~29 KB.
- **KPF2:** the 8 settings after KPF1 (tag, count, bytes), clamped on load. Older projects get the defaults.
- **Tests** (ui_test, 69 screens):
  - HALF (48 ticks in 0.5 s, against 96), DRILL (17 hits a bar for 4), JITTER (most hits late, never past 12 ticks), and latching across banks (a key letting go in its own bank).
  - SHORT (the kick's tail under 10%), PUMP (down at the hit, back after an eighth).
  - COMB (the impulse response correlates 0.82 at A2's period and 0.00 off it), CUTS (43% shut at a ~50% setting), CRUSH (held, 4 bits), REVERSE (exactly the recording backwards), and the SETUP pages and KPF2 round trip.
  - CPU on hardware: not yet measured. Each is on only while held or latched; COMB and REVERSE are the dearest, a few multiplies a sample.

The plan as agreed:

A second set of PERF effects, each cheap on its own, picked for the user's influences. In PERF, SELECT gets two more pages: **PERFORM 2** and **PERFORM 2: SETUP**. While PERFORM 2 is shown, keys 1–16 play the second bank; the black keys (mutes) and the bar keys (repeat length) stay as they are. Latched effects from both banks keep running when switching pages. Effect settings are saved with the project (a KPF2 section after KPF1).

| Key | Name | Does | Influence | Cost |
|---|---|---|---|---|
| 1–7 | **DRILL** | The track's hits become random fast ratchets (2–8 hits, 1/32–1/64, random velocity and small pitch steps) while held | Aphex drill'n'bass | sequencer only |
| 8 | **HALF** | Half-time: the sequencer at half speed, voices unchanged | dub techno, Autechre | none |
| 9 | **JITTER** | Each hit shifted at random by up to ± a 32nd | Autechre, IDM | none |
| 10 | **CUTS** | The master gated by a random 1/32 pattern with tiny fades: clicks and cuts | microhouse | ~0.3% |
| 11 | **SHORT** | Every voice's decay cut to a click | microhouse | none |
| 12 | **PUMP** | The master ducked by the kick (D1) envelope, sidechain style | house, dub techno | ~0.2% |
| 13 | **COMB** | A tuned comb resonator on the master (pitch and feedback in SETUP): drums ring as a chord-like tone | dub techno, S-4 | ~1% (8 KB) |
| 14 | **SPACE** | The reverb frozen into a pad (feedback held, input open), the dry sound dipped | dub techno | ~0 |
| 15 | **CRUSH** | Bit depth and sample rate on the master (amount in SETUP) | Aphex, lo-fi | ~0.3% |
| 16 | **REVERSE** | The last 1/8 note played backwards (a 0.25 s recorder runs all the time, mono, 22 kHz) | S-4, IDM | ~0.5% (11 KB) |

**PERFORM 2: SETUP** knobs (two pages): drill density, jitter amount, cuts density, pump depth / comb pitch, comb feedback, crush amount, reverse length.

Held back (cheap, but didn't make the 16; can swap in on review): PITCH WARP (every voice bent up or down an octave while held), SCATTER (random pan per hit), RING (the master ring-modulated at a tempo-synced rate), FORMANT (a vowel sweep), TAPE (wow and flutter through a 20 ms modulated delay), DENSE (ghost notes on empty steps), SWING PUSH. Too heavy here: the S-4's granular clouds and time-stretch (a simple pitch shifter with two crossfaded delay taps, ~3%, is the most that fits).

Tests: each effect against its own render check (CUTS silence fraction, PUMP gain at the kick, COMB's tone at its pitch, REVERSE equals the recorded buffer backwards), latching across banks, project save and load.

### 5d. Physical-model voices (built)

What was built, and where it differs from the plan:
- **Where it's chosen:** not an Engine switch on the OSC page but the first knob of the new **MODEL** section (EDIT, after CLICK; drum tracks only, `S_MODEL`): Model = LXR is the LXR's engine. 7 knobs (Model, Decay, Damping, Material / Position, Exciter, Tension), not 8: Pitch stays the voice's own Coarse and Fine on the OSC page. MEMBRANE is shown as **SKIN** (fits the knob card).
- **Parameters** (`sound.c`): `PAR_MODEL1` .. `PAR_MTENS1`, six each like Elixir's other per-voice settings (4..6 unused), NRPN 256–297. Defaults (`kit.c`): decay 64, damping 40, material 64, position 40, exciter 32, tension 0. All lockable per step; Model is a menu and doesn't morph (`morph_excluded`). Old kits and projects load as LXR: the project stores `NPAR` and its count.
- **The engine** (`voices.c` `model_t mdl[3]`, `model_trigger`, `model_coefs`, `model_block`): 6 two-pole resonators a voice, the coefficients recomputed each block from the oscillator's increment (so the pitch EG, random pitch and TENSION's fall all bend it), sin/cos from a Q30 Taylor series (`sin_q30`; `cos w` as 1 − 2 sin²(w/2) to stay exact at low pitches). Modes past 0.45 FS are dropped.
  - Decay 20 ms × 2^(decay × 0.06) to −60 dB (BELL and PLATE ×2). Damping multiplies each mode's decay rate by 1 + (ratio − 1) × damping / 32. Material raises the ratios to the power 0.7..1.3 (`exp2_q16` of the log2 tables `MD_LOG2`).
  - Position: each mode's level is |sin(π p (k+1))|, p = 0.02..0.5, floored at 0.1 so no mode vanishes. Velocity: the higher modes louder for a harder hit (mode k × 1 − (1 − v) k / 8).
  - Exciter: a half-sine pulse of 190 samples (thump) down to 1 (click), or a noise burst of 1.5–7 ms.
  - TUBE: Karplus-Strong in a 1,104-sample line a voice in the pool (6.6 KB for three), fractional delay, a one-pole low-pass in the loop (Damping) and a loss a pass from Decay. It ignores Material and Position.
- **The amp EG is bypassed** for a model (the model's Decay is the voice's length), and so is velocity's volume mod (velocity drives the strike instead). After the model: the voice's filter, then the shape/drive, bits, tilt, sends, pan, cross modulation, as planned.
- **A bug found by the tests:** the LXR's sine table (`LXR_SINE`, `sine_at`) is −cos, not sin. The model's position and pulse use `sine_at(ph + 2^30)`.
- **Pool:** 348.7 KB of 360 KB used, ~19.5 KB free (TUBE's lines). Image 290 KB.
- **CPU:** not yet measured on the FM-1. Per voice ~6 × 3 multiplies a sample while a model is on; nothing when Model = LXR. If three models are too heavy, the resonators are the second core's first job.
- **Not built:** the SNARE model (5d+), as planned.
- **Tests** (ui_test, 70 screens): each model's modes at their ratios (Goertzel at each ratio against its ±3% neighbours, at 523 Hz with damping 0); SKIN's decay at 64 (−20 dB over 93 ms); velocity brightness (the 6th mode 0.18 of the 1st hard, 0.09 soft); TUBE's period (84 samples at 523 Hz); TENSION up at the hit and back; the MODEL page from EDIT; Model back to LXR; the project round trip.

The plan as agreed:

A second engine for the three DRUM voices: instead of the LXR's oscillators, a struck resonating body.

- **On the voice's OSC page:** **Engine: LXR / MODEL**. With MODEL, a new EDIT section **MODEL** (8 knobs, all lockable and macro targets):
  - **Model:** MEMBRANE (drum head: modes at 1, 1.59, 2.14, 2.30, 2.65, 2.92), BAR (marimba, xylophone: 1, 2.76, 5.40, 8.93), BELL / BOWL (inharmonic, long), PLATE (dense, metallic), TUBE (a Karplus-Strong string or pipe, plucked or blown).
  - **Pitch** (the voice's own coarse and fine), **Decay**, **Damping** (how much faster the high modes die), **Material** (stretches the mode ratios: wood to glass to metal), **Position** (where it's struck: which modes sound), **Exciter** (mallet hardness: a soft thump through a click to a noise burst, using the LXR's transients), **Tension** (the pitch drop of a hit drum head).
- **Built:** 6 two-pole resonators per voice in fixed point (2 multiply-adds per mode per sample), mode coefficients recomputed only when a parameter moves. TUBE: a delay line of up to 1,100 samples (2.2 KB per voice) with a damping filter. The voice's filter, waveshaper, bits, tilt, sends, random per hit and cross modulation all still apply after it.
- **Velocity** sets the strike force and brightness (harder = more high modes), as real drums do.
- **Later (5d+):** a MODEL engine for the SNARE (a membrane whose level gates its noise as snare wires).
- **CPU:** ~20 operations a sample per voice, about what the LXR's own drum oscillator costs: an estimate, to be measured on the FM-1 before going further. If three voices cost too much, the resonators are the obvious job for the second core.
- Kit parameters grow by ~10 a voice (about 2 KB of pool for 64 kits). Old kits load as Engine: LXR.
- Tests: each model's partials at their ratios (FFT of a hit), decay times, the TUBE's pitch, velocity brightness, the kit round trip.

### Decisions (review, 2026-10-24)

1. **Order:** the light parts first: 5a → 5b → 5c → 5d.
2. **PERFORM 2:** the 16 as drafted.
3. **DRILL** on keys 1–7 of bank 2, as a trial: scrapped (back to rolls there) if it doesn't sound good.
4. **Coupling:** 8 links a pattern. The UI must be easy to find and to read, with many screens already:
   - LINKS is the EUCLID group's fifth page, and also opens directly with SHIFT + SEQ (on the SHIFT legend).
   - A track with links shows a small mark on the grid, so coupling is visible where the pattern is.
   - One link per screen, as a sentence; an empty link offers the starting points first, so a useful link is one knob turn away.
   - The manual gets a short worked example.
5. **Models:** the engine is the same code for the three DRUM voices, so it is built for all three, but measured on hardware with one model voice, then three, before the SNARE model.

**Rule for this phase:** anything that turns out much harder than planned is noted here and skipped, not forced; we come back to it later.

## Phases 6–10: refinements (planned 2026-10-24)

The user's list (October 24): the name and the repository, the physical models' sound, LXR-02 kits on the unit, backing up what's on the unit, and checking that Elixir has what makes people love the LXR-02, not only its engine.

**Name (decided):** Kettle stays. (Renamed Elixir in Phase 15.) The LXR firmware's licence terms (its README and NOTICE.md, GPL-3.0 section 7(e)) don't allow "LXR" in a firmware's name without Erica Synths' written permission; truthful compatibility statements are fine. The code is at https://github.com/dmajka/kettle (`REPO` in `web/make_site.py`).

**Kits (decided):** the factory and Hrtl kits aren't in the GPL source and come with no licence to redistribute, so: (1) an import tool reads the user's own .SND files, and (2) a built-in bank is made from a local kit folder at build time, behind a build flag, off in public builds.

### Phase 6: the physical models, revisited (built in 0.7)

Built as planned (`voices.c` `model_trigger`, `soft_clip`). Every model, exciter and pitch now peaks at 0.17–0.84 of full scale (ui_test "MODEL: a hit's level"); the 0.6 demo phrases no longer clip anywhere. Waiting for the user's listening test; a model that still isn't musical gets removed or reworked then.

Reported: BELL and others get very distorted, and some don't sound musical.

Measured (a host probe, every model × exciter × pitch × position): a full-velocity hit peaked anywhere from 0.04× to 6.2× full scale, a 44 dB spread, and clipped hard inside the model. The causes:
- The six modes each rang up to about full scale and were added before a hard 16-bit clip.
- The exciter was a fixed length in samples: a soft mallet was nearly silent on high notes, and the shortest noise burst rang 6× over.
- TUBE's soft pluck was 40 dB too quiet (0.01×).
- Retriggering a long ring stacked hits.

Plan:
- The exciter sized to the hit's pitch (a soft pulse up to 3/4 of a period, area kept; noise by energy).
- The modes' shares normalised to a fixed sum.
- A new hit damps what still rings by half.
- A soft knee instead of the hard clip.
- TUBE: a one-period pluck at a fixed peak, its noise darkened by EXCITER.
- A test: every model, exciter and pitch within 0.15–0.95 of full scale.
- Then the user listens (before/after WAVs). Any model that still isn't musical is removed in a later step.

### Phase 7: the Elixir library — backup, restore and kit import over USB (built in 0.7)

What was built, and where it differs from the plan:
- **Device:** `firmware/src/library.c`, one entry point (`lib_in`) for a whole message, used by the firmware (`lib_service` in the main loop, before `ota_service`: a frame that isn't the library's is left for the updater), the emulator (`web_lib_in`, the worklet's "lib" message) and the host bridge. The protocol is in its header.
- **Commands:** besides the plan's, INFO also gives the loaded slots, KIT_NAMES lists the kits, and KIT_GET exports a kit. So the page shows and exports kits too.
- **Writes:** a restore goes to a free physical slot, with its header written last after the payload's CRC is checked in flash. While it is open, `proj_lock` holds off autosave and SAVE; it gives up after 10 s without data, and the output is hushed throughout.
- **Kits:** KIT_PUT takes any length up to KIT_LEN. An LXR .SND's 247 bytes get Elixir's defaults for the rest, `kit_sanitize`, and their names cleaned. LXR "Out = FX BUS" becomes FX SEND 127 (204 of 15,314 bytes in the factory PROJ00, all of them outputs).
- **Pages:** `web/library.html` (the site's `library/`) for the FM-1, and a Library panel on the emulator page. Backup files: `.ktbk` (several projects) and `.ktp` (one project's image as it is in flash). Both are self-checking (the header's CRC and the payload's).
- **Tests:** `tests/library_bridge.c` + `tests/library_test.mjs`:
  - the 62 factory kits in and back (byte for byte, apart from the FX bus outputs);
  - a backup read, restored into another slot (identical payload) and loaded;
  - a damaged image refused, data before BEGIN refused, SAVE refused during a restore and allowed after the 10 s timeout;
  - a garbled message unanswered.
  - Also checked in Chrome on the emulator: three Hrtl kits imported (the Init one skipped) and the first loaded.
- **Not done:** the transfer over a real FM-1's USB is untested (the SysEx path is the updater's, which works). The first hardware test should back up, restore into another slot and load.

Today nothing on the unit can be copied off it. Elixir's projects sit in flash at 0x97000–0xDEFFF. Elixir's own updates don't touch that area, but V15 or another firmware may, and nothing protects against a bad flash.

- **Protocol:** SysEx on USB, beside the updater's (F0 7D 'K' 'T' cmd … F7, 7-bit packed, 256-byte chunks, a checksum per message). Commands:
  - info: version and the project slots' names, sizes and versions
  - read a project's image
  - write a project's image to a slot (a free physical slot; header last, as `proj_write` does, so a torn restore leaves the old copy)
  - write a kit: an LXR .SND file's 8 + 247 bytes into a kit slot of the session
  - save the session into a slot (so a backup includes what's playing)
- **Page:** `web/library.html` on the site (Web MIDI, Chrome/Edge):
  - Backup: every project to a file, or one project.
  - Restore: a file into a slot.
  - Import kits: pick .SND files or a whole LXR project folder; they fill kit slots from a chosen number; then SAVE PROJECT on the unit (or let AUTO keep them).
- **Tests:** the device side in a host test; the page's protocol code against the device code through a host bridge (no hardware needed).

### Phase 8: LXR-02 kits on the unit (built in 0.7)

Built as planned.
- **The bank:** `tools/gen_factory_kits.py` takes `ELIXIR_KITS_DIR` as it is (':' between folders) and leaves out repeats by content. From the factory PROJ00, PROJ05 and PROJ01, Hrtl's UNTZ and DLC kits: 111 kits, about 28 KB of flash (image 323 KB against 294 KB).
- **On the unit:** `engine_init` fills the slots, so it only shows on a unit with no session. LOAD → FACTORY loads a kit into the edit buffer (the header still names the slot's kit until it's saved), or ALL > SLOTS.
- **Publishing guard:** `web/make_site.py` looks for "unofficial port, …+K" in `build/elixir.bin` and `build/emu/elixir.wasm`.
- **Tests:** checked in Chrome on a +K emulator: it starts with Badonka, the FACTORY page lists the bank, and a factory kit loads.

- `tools/gen_factory_kits.py KIT_DIR` reads .SND files from a local folder (the factory SD image's PROJ00, Hrtl's UNTZ and DLC kits) into `build/gen/factory_kits.h`, which is never committed.
- `ELIXIR_KITS_DIR=… ./build.sh` (and `web/emu/build.sh`) builds them in. Without it, the build is as today.
- With the bank built in:
  - A fresh unit (no session) starts with the bank's first 64 kits in its slots.
  - LOAD → FACTORY copies them in again on request.
  - The version shows "+K".
- `web/make_site.py` refuses to publish a "+K" build.
- The import tool (Phase 7) is the public route.

### Phase 9: the LXR-02's live feel (built in 0.8)

What was built, and where it differs from the plan:
- **9a, LXR patterns:** the library page imports a whole LXR project folder: its kits into the slots by number, its .PAT patterns (V1, V2 and V3 formats) and .SNG songs. Elixir has 32 patterns, so the user picks the LXR's first or second half; a song step on the other half plays pattern 32. New SysEx: PAT_BEGIN / PAT_STEPS / PAT_END (0x32–0x34), SONG_PUT (0x35).
- **9b, MIX in PERF, and the page order** (the user asked whether performance, macros, more performance was confusing; it was): PERFORM (BPM, Roll, Morph, SRate: the LXR's PERFORMANCE page), PERFORM: MORE (DJ filter, Repeat, Shuffle, Tape), PERFORM: MIX (D1–SN volumes), MIX 2 (CY, HH volumes, FX dry/wet, reverb level), MACROS, MACRO SETUP, then PERFORM 2 and its setups. A held roll puts the swept roll on KNOB 2.
- **9c, KEYS:** SHIFT + EDIT. The keys play the track's voice from F (octave on KNOB 1); notes go out on MIDI and into the steps under REC (as MIDI notes in, through the audio side's queue). The sound pages opened from KEYS keep the keys playing.
- **9d, key velocity:** KEYS' KNOB 2 and 3: the velocity of keys, rolls and auditions, and the accent (SHIFT). Kept in the settings.
- **9e, sound browser:** PRESETS on a sound page or in KEYS: the library's sounds, then the voices of the library's kits, the slots' kits and the factory kits (a drum track: all three drums of each; init voices left out). Each step auditions; OCT+ keeps, OCT− reverts.
- **9f, kit library:** `firmware/src/kitlib.c`. 128 kits and 128 sounds in flash at 0xEA000–0xFBFFF (18 sectors: a log of 256-byte pages, the commit record written last, one sector kept erased for compaction). SAVE / LOAD → KIT and MORPH take To / From SLOTS or LIBRARY; SAVE / LOAD → SOUND keeps one voice (a drum's into any drum). The library page lists, backs up (`.ktlb`), restores and imports LXR kits into it (SysEx KL_LIST / KL_GET / KL_PUT, 0x40–0x42). Not yet checked on a unit: that stock V15 and the installer leave 0xEA000–0xFBFFF alone (the stock store is 0x93000–0xE8FFF, and Elixir's updater doesn't write there).
- **9g, SRate:** on PERFORM's KNOB 4 ("SRate"), lockable, so it is a macro target and records under REC.
- **Tests:** ui_test phase9 (PERF pages, MIX, KEYS, the library's SAVE / LOAD, the browser), library_test sections 7 and 8 (an LXR project in, V2 and V3; the kit library: list, get, put, delete, backup, restore, a reboot's scan).
- **Still open:** the user's comparison with LXR-02 videos (what still feels missing).


The audit (Elixir against the LXR-02 manual and firmware):
- **Already ported:** every voice parameter, per-voice LFOs (synced, retriggered from any track, any target), velocity to any parameter, two locks a step, knob recording, rolls (recorded under REC; a single hit when stopped), morph, global sample rate, shuffle, mutes with PERF's unmute-all, sound editing from PERF with the keys still playing, kit change with patterns, reload, songs, Euclid, chromatic MIDI play (note ANY), step notes from a MIDI keyboard.

What's missing is mostly what you meet first:
- **The kits:** the LXR-02's sound in demos is hrtl's kits, which lean on LFOs, velocity routing and locks. Elixir starts on the init kit. (Phases 7–8.)
- **Demo patterns:** the factory and DLC projects come with patterns.
- **Faders:** six volume faders, ridden while playing. The FM-1 has four knobs and the mixer is three presses away.
- **Playing it:** the LXR's voices are played melodically from MIDI. The FM-1 is a keyboard, but Elixir's keys are steps.

Planned:
- **9a. LXR patterns:** .PAT import in the library, so a whole LXR project (kits, patterns, songs) becomes a Elixir project. The demo patterns come with it.
- **9b. MIX in PERF:** a PERF page with the voices' volumes (two pages: D1 D2 D3 SN / CY HH, FX return, reverb return), so levels can be ridden like the faders.
- **9c. KEYS:** the white and black keys play the selected voice chromatically, as an instrument; REC writes the notes into steps (note and velocity), as MIDI notes do now.
- **9d. Key velocity:** the FM-1's keys have no velocity. A setting gives the velocity of the keys and rolls, and SHIFT plays an accent.
- **9e. Sound browser (per track):** select a track (say D1) and browse, audition and load single-voice presets taken from every kit's voices of the same kind: the D1, D2 and D3 voices of all kits for a drum track, SN for SN, CY for CY, HH for CH/OH. Save the current track's sound as a preset too. Not in the LXR-02; it works like the Digitone's or Digitakt's sound browser. (Requested 2026-10-24.)
- **9f. A kit library in flash, and morph from anywhere:** more of the user's own kits without more RAM. The 64 RAM slots stay as they are (the pool has ~18 KB free and keeps 8 KB spare; Phase 10 should free ~7 KB, which isn't spent on kits). Saved kits and 9e's single-voice presets go to a store in flash outside the projects, read one at a time as FACTORY is. About 108 KB after the OTA area (0xE5000–0xFFFFF) looks unused, room for ~250 kits; first confirm stock V15 and the installer leave it alone. LOAD and LOAD MORPH take a kit from a slot, FACTORY or the library: morph needs only the playing kit and the target in RAM (0.7 KB). The library page backs the store up. (Requested 2026-10-24: "let's not use up all of our RAM on kits".)
- **9g. The global sample-rate reducer, easy to reach:** the LXR's PAR_VOICE_DECIMATION_ALL (its PERFORMANCE page: BPM, Roll, Morph, Sample rate, Shuffle) is ported exactly (voices.c, mixer_decimateBlock: each voice's own rate × the global one, before the mix), but it is hidden on PERFORM: MORE, KNOB 3 ("All SRate"). The LXR's global crush is a sample-rate reducer, not a bit-depth one. Elixir's extras are separate: per-voice Bits (Shape page) and PERF 2's CRUSH effect on the output. To do: put it on a main PERF page next to Morph (the LXR's own layout), check that it is a macro target and that knob recording records it, and check it against the LXR's sound (it is a kit parameter there, so a kit carries it). (Requested 2026-10-24.)
- After 9: the user compares with LXR-02 videos and lists what still feels missing.

### Phase 10: Rings in place of the physical models (built in 0.9)

Requested 2026-10-24: the user isn't fond of how the Phase 5d/6 physical models sound.

What was built:
- **The port:** `firmware/src/rings.c`, a fixed-point port of Rings' modal resonator (`resonator.cc`), string (`string.cc`, with its dispersion, curved bridge and the plucker) and FM voice (`fm_voice.cc`, with its follower), Part's exciter filter and its limiter, with the stmlib DSP they use. Its tables come from `tools/gen_rings_tables.py`. Rings is **MIT-licensed** (not GPL as first noted); its notice is in `LICENSES/MIT-Rings.txt` and NOTICE.md.
- **Models:** LXR, MODAL, STRING, FM on Drum 1–3. The knobs are Rings': Decay (DAMPING), Bright (BRIGHTNESS), Struct (STRUCTURE), Position (POSITION), plus Exciter (Rings' own exciter at 0, the voice's LXR sound as Rings' audio input at 127, a mix between) and Tension.
- **Left out:** the sympathetic strings (several strings a voice: CPU), Rings' other modes, its reverb (Elixir has its own) and its polyphony (a drum voice is one).
- **Cost:** MODAL runs 32 modes with one drum on it, 24 with two, 16 with three (Rings: 60, 28, 12 by polyphony). The string's lines are 32-bit and live in fast RAM (18 KB for three voices): in 16 bits, a long decay's tail sticks at a level. The line is 1024 samples (43 Hz); lower notes run the string at a lower rate, as Rings' does. CPU on the FM-1 not yet measured: the first thing to check on hardware, with three drums on MODAL.
- **Accuracy:** `tests/rings_compare.cc` (with `tests/rings_core.c`; `tests/rings_compare.sh` needs Rings' and stmlib's sources) renders 18 cases (pitches from 30 Hz to 1 kHz, every model, their parameters, impulses, bursts and the FM envelope) with the port and with Rings' float code at 44.1 kHz: correlation 0.9998 and up, the error −35 dB or lower. Where Q20 alone wasn't enough, the port keeps more bits (the modes' rounding error fed back, the SVFs' and DC blockers' Q26 states, the FM envelopes' Q28, a finer ratio for the FM modulator, a true sine table: the LXR's is off by half a step), and replicates stmlib's 1/256-semitone pitch steps.
- **The old models:** their code is kept, unbuilt, in `firmware/src/physmodels.c`. Projects (version 7 now) from before map SKIN, BAR, BELL and PLATE to MODAL and TUBE to STRING (`proj_models_08`).
- **Tests:** ui_test phase5d (MODAL's harmonic and stretched partials, STRING's and FM's pitch, STRING below its line, DECAY, the level over models, BRIGHT, STRUCTURE and pitch, EXCITER, velocity, D1 through the voice, TENSION, the page, a project, version 6's mapping).
- **Next:** the user listens, and the CPU is measured on a unit.

### Pattern edits kept on a switch (built in 0.9)

Requested 2026-10-25: losing a new pattern's work on a switch broke the flow; on a Digitakt the patterns are simply part of the project.

- **Edits** on the pattern page (KNOB 3): KEEP (the default) leaves a pattern's changes in place when another starts; DROP is the LXR's way (they go unless saved with SAVE → PATTERN). A setting (`kset.pdrop`, so settings from before read as KEEP). AUTOSAVE already writes every pattern to the session.
- **RELOAD** still reverts the playing pattern to how it was when it started (or was last saved), on either setting (`pat_revert`).
- **Tests:** ui_test phase9_patkeep (KEEP stopped and playing, RELOAD, DROP, the setting).

### 0.9.1: trying kits in the emulator (built)

Requested 2026-10-25: try LXR-02 kits in the emulator before putting them on the unit, without the emulator holding more than the FM-1 does; and a clearer library page.

- **Kit browser:** PRESETS lists the 64 slots or (0.9.2, at the user's request: browsing the library after 64 slots was awkward) with KNOB 1 at From LIBRARY the kit library's kits (L1 …, the empty ones left out); it opens where it was last; OCT+ loads a library kit as LOAD → KIT From LIBRARY does (`kb_has`, `kb_step`, `kb_start` in ui.c).
- **Emulator page:** its Library imports .SND files or folders into the kit library (the default) or the slots, imports an LXR-02 project, and downloads / restores the kit library (`.ktlb`, which the library page restores onto the FM-1). The emulator stays 1:1 (64 slots, 128 library kits): more kits mean another batch.
- **Library page:** a Kit library section with its own import (files or a folder) and a Kit slots section with the slots' import and the LXR project import; text on what each is for (slots: the project's, named by patterns; library: a collection across projects, the morph target, the sound browser's voices); folders are read with their subfolders (a whole SD card: every project's kits, in order); a project import refuses a folder holding several projects (`lxrFolders`).
- **Tests:** ui_test (the kit browser over the slots and the library), library_test (a card's subfolders, the several-projects refusal).

### Phase 11: the track pages, made plain (built in 0.10)

Requested 2026-10-25, from looking for the generator page:

- **11a. SEQ like every other button.** SEQ toggled between step edit and the page it came from (`ui.back`), so a second press left step edit, where every other button shows its next page. It was on purpose (a way back to a sound page), but it's the one exception, and the way back is the sound's own button anyway. SEQ again now steps through STEP → TIMING → LOCKS → RATCHET, as SELECT does; another button leaves.
- **11b. One generator page.** Euclid was its own page and the generators (MUTATE, TURING, CELLULAR, GRIDS) the next one, though both are a way of making a track's steps. Now the first page is **GENERATOR**: KNOB 1 the type (OFF, EUCLID, MUTATE, TURING, CELLULAR, GRIDS), KNOBS 2–4 that type's settings (Euclid: length, hits, rotation). EUCLID still writes the steps (the LXR's way; no live copy) and is kept as a generator mode that doesn't play live (`GEN_EUCLID`, added last so saved projects keep their numbers), so its settings belong to the pattern's track and are saved, and a generator turned on afterwards works on the Euclid steps.
- **11c. Euclid starts empty.** EUCLID starts at 0 hits with the track's length and nothing written; the steps change once the hits are turned (back to 0 clears them). Before, it showed 4 hits that weren't on the track until another knob moved.
- **11d. The action, findable.** The second page is **ACTION** (GRIDS: **GRIDS MAP**, the map's X, Y, chaos on KNOBS 2–4). The actions are the type's: EUCLID DICE, UNDO; a live generator COMMIT, UNDO, RESET; OFF UNDO. The card reads "OCT+ to …" while one is picked.
- **11e. The Grids map only with Grids** (11d): no GRIDS MAP page on a track without GRIDS.
- **11f. Play direction per track** (as on the Polyend Play): FWD, REV, PING (ping-pong, the ends once), PING2 (the ends twice), RAND (any step), WALK (a step either way, at random), on the **TRACK** page with length and clock (the bounce's start and pitch move to a **BOUNCE** page). A time round stays the track's length in steps, so A:B conditions, generators and the pattern's length keep their timing; the next step is drawn a step ahead, so negative shifts and flams still see it. A pattern byte per track (project version 8).
- **Also fixed:** the title didn't redraw when only its text changed on the same page, so the old "OCT+: DO IT" never showed (`draw_title`'s signature now covers the text).
- **Project format:** version 8 (the directions). The 21 bytes that held the per-track Euclid settings (not the pattern's then) stay in the format, written as 0 and skipped.
- **Tests:** ui_test phase11 (SEQ's pages, the generator types, Euclid from 0 hits, MUTATE over Euclid's steps, the actions by type, the map only with GRIDS, each direction's order of steps, a time round under PING, a negative shift going backwards as forwards, the TRACK page, project version 8).
- **11g. Back to init, one sound** (requested 2026-10-25: "I go crazy turning knobs … I just want the individual sound back to an init state"): CLEAR has **CLEAR SOUND (INIT)**, the selected track's voice as the init kit has it (all of its parameters, Elixir's too), the other voices untouched. On a sound page or in KEYS, CLEAR opens on it, so SHIFT + OCT+ twice does it (`sound_init`).
- **11h. Solo** (requested the same day): **SHIFT + GLO** solos the selected track, from any page (shaping a sound in a busy pattern). Several can be soloed; the mutes stay underneath (`sq.solo`, `seq_mutes`: what the sequencer, the rolls, the links and MIDI notes in treat as muted). The grid marks a soloed track, the others show muted. Not saved.
- **Tests:** ui_test phase11_sound_solo (CLEAR SOUND on a sound page: that voice to init, another left; solo: one track, two, off, the mutes kept).
- **Next:** the user tries it in the emulator and on the unit.

### Phase 12: a remote control map for the Launch Control XL (12a–12e built in 0.12)

**Built** (2026-10-25). Decided with the user: the rows as proposed (below, as built), buttons on **notes** (so the knob rows keep the CC space; note off is the release), remote knobs **record locks** on their own track under REC, **PICKUP** the default knob mode.
- **12a.** NRPN 120 (morph) goes through `kit_set_morph` (0–127 scaled to 0–255), 119 (roll rate) sets `sq.roll_rate`. Tempo by NRPN already worked (engine.c reads `par[PAR_BPM]`), 0–127 only.
- **12b.** CONFIG has an 8th page, **REMOTE**: Channel (OFF, 1–16; default 16) and Knobs (PICKUP, ABS, REL), in the settings (`kset.remote`: 0 from older settings means 16, 17 OFF; `kset.rknobs`). The page warns when the channel is a track's or the global one. In midi.c, notes and CCs on the remote channel go to a queue for the main loop (`rin_q`) before the filters and the LXR's map see them.
- **12c. The map** (`remote.c`): knob rows at CC 8 + 8 × row + column, rows: volume, pan, filter, resonance, decay, pitch, drive, FX send, reverb send, sample rate, LFO amount, random pitch, macros 1–4 + DRILL / JITTER / CUTS / PUMP settings, random decay; the master column FX dry/wet, reverb size, DJ filter, FX P1, tape time, tempo (60–187), SRate, FX P2, reverb level, shuffle, morph, roll rate, (PUMP), repeat length. Buttons at note 36 + 8 × row + column: mute, solo, select, roll (held), DRILL (held), the eight effects TAPE HALF CUTS PUMP COMB SPACE CRUSH REVERSE held, the same latched, prev / next pattern; the master column PLAY/STOP, REC, FILL, REPEAT, FREEZE. Knobs go through `vp_set`, as the panel's. The remote's held and latched effects and DRILLs are their own (`ui.rm_held`, `rm_latch`, `rm_drill`; `perf_apply` joins them with the panel's), so leaving PERF doesn't drop them.
- **12d.** PICKUP: a knob acts once it is within one of the value or has crossed it since its last message; when the value changes elsewhere (seen by the feedback scan) it waits again. REL: 64 ± n, or two's complement (v < 32 up, v ≥ 96 down); the tempo a BPM a step (20–255); wide ranges (morph) two a step.
- **12e. Feedback:** the main loop scans the map every 4 ms and sends up to 12 changes (a queue to the audio ISR, `rout_q`, which owns USB MIDI out): a knob's value as its CC, a button's state as its note (127 / 0). What the remote itself sent isn't echoed. All of it is sent again when the channel or mode changes or USB is configured again. The kit's values (a lock playing isn't sent; morph's mixture isn't either).
- **Costs:** about 0.8 KB of RAM (the two 64-entry queues and the map's state), 3.5 KB of code; no project format change.
- **Tests:** ui_test phase12_remote (ABS on a track, the master column, morph, DJ filter; CH and OH decays apart; the LXR's CC 38 not taken on the remote channel; tempo's range; PICKUP waiting, following, waiting again after a panel change; REL both encodings and the tempo; every button kind, a latch surviving PERF's exit, patterns, PLAY / STOP; a lock recorded on the knob's own track; feedback of a panel change, a kit load and a mute, no echo; NRPN morph and roll; REMOTE OFF).
- **Still to do:**
  - **12f, the templates:** build them in Novation Components for the mk2 and mk3 (rows chosen per template: A Mix / Perform, B Sound, C Mangle, as below), export, and keep them in `tools/launchcontrol/` with a page in the manual. Needs the controllers.
  - **On the hardware:** whether the mk2 lights its user template's LEDs from notes in (or only by SysEx), whether the mk3's rings follow CCs in (its custom mode may need "receive"), how the mk3's encoders send relative values. If the mk2 needs SysEx for its LEDs, feedback grows a mode for it.
  - **Faders in REL:** the knob mode is global, so with REL the faders (absolute) would misbehave; a per-row mode, or faders on rows of their own, if the mk3's encoders turn out to need REL.

The plan as written:

Requested 2026-10-25: a CC table of what MIDI can reach, then performance templates for a Novation Launch Control XL mk2 and mk3, with seven columns for the tracks and the eighth for the master. The table is in MANUAL.md §17 (*The sound by CC*, *The rest by NRPN*). Templates built on the LXR's map would hit three walls. The performance controls can't be reached at all: mutes, solo, morph, tempo, roll rate, DJ filter, repeat, tape, PERF and PERFORM 2. Elixir's own parameters (sends, reverb, shaping, models) are NRPN only, and the mk2 can't send NRPN (the mk3's support is unconfirmed). And nothing goes back to the controller, so the mk3's encoder rings can't show the values and the mk2's pots jump. So Elixir gets a second CC map, made for an 8-column controller. The LXR's map stays as it is.

- **12a. MIDI morph, roll and tempo (a bug).** NRPN 120 (morph), 119 (roll rate) and 144 (tempo) write `par[]` and nothing more. Morph lives in `morph_amt` (`kit_set_morph`) and the roll rate in `sq.roll_rate`, and the tempo takes only 0–127, where 0–1 means MIDI sync. Route them through the same setters the panel uses, or leave them out of `midi_param` and reach them through the remote map only.
- **12b. The remote channel.** CONFIG: MIDI gets **Remote**: OFF or 1–16 (default 16; it shouldn't be any track's channel or the global one, so it is checked against them). CCs on it go to the remote map and nowhere else. The setting is saved with the other settings.
- **12c. The map.** Rows of eight CCs: the CC is the row's base plus the column (1–7 the tracks D1 D2 D3 SN CY CH OH, 8 the master). For a voice parameter, CH and OH both act on the HH voice, except where OH has its own (decay). Proposed rows, to be fixed in the manual with the numbers:
  - Volume (column 8: the FX bus dry/wet), Pan (8: reverb size)
  - Filter frequency (8: the DJ filter), Resonance (8: the FX bus P1)
  - Decay (8: tape time), Pitch / coarse (8: tempo, relative only), Drive (8: global SRate)
  - FX send (8: FX P2), Reverb send (8: reverb level), Sample rate (8: shuffle)
  - LFO amount (8: morph), Random pitch (8: roll rate)
  - Macros 1–4 and the PERFORM 2 settings (drill, jitter, cuts, pump, comb, crush) on one more row or two
  - Buttons (value > 0 down, 0 up): mute (toggle), solo (toggle), select, roll (held), DRILL (held) per track. Master column: PLAY/STOP, REC, FILL, repeat, freeze, tape stop, HALF, CUTS, PUMP, COMB, SPACE, CRUSH, REVERSE (held, as the keys; a second CC row latches), next / previous pattern.
- **12d. Knob modes** (Remote: **Knobs** ABS / REL / PICKUP). REL takes the mk3's relative encoders (64 ± n, and two's complement if the mk3 sends that), so nothing ever jumps. PICKUP is for absolute pots (mk2): a pot does nothing until it crosses the current value. ABS sets the value directly.
- **12e. Feedback.** When a remote parameter changes from anywhere (a panel knob, a kit or pattern load, morph, a lock playing back is *not* sent), Elixir sends its remote CC out on the remote channel. Mutes, solo and latched effects send their state for the buttons' lights. Out is USB only: the FM-1 has no MIDI out jack. So feedback reaches the controller only when both are on a USB host (the mioXL, or a computer), not with the mk3 on DIN straight into the TRS input. Rate-limit it (one pass over the changed values per few ms) so a morph sweep doesn't flood the USB. Check how each model takes LED and ring values: the mk3 may need its custom mode set to "receive" for the encoder rings, and the mk2's user templates may light LEDs only through SysEx.
- **12f. The templates.** Build one per controller in Novation Components (mk2: 8 user templates; mk3: 15 custom modes) and export it. Generate the rest from the map if the file format is readable, and keep them in `tools/launchcontrol/` with a page in the manual. Three to start:
  - **A: Mix / Perform.** Faders volume; knobs filter, reverb send, decay; buttons mute / solo. Master: fader morph, knobs DJ filter, FX dry/wet, reverb level, buttons for the effects.
  - **B: Sound.** Knobs pitch, decay, drive; fader sample rate; master SRate, shuffle, reverb size.
  - **C: Mangle.** Knobs random pitch / decay, LFO amount; buttons DRILL per track; master HALF, CUTS, PUMP, COMB, REVERSE.
- **Connecting.** mk3: its DIN MIDI out into the FM-1's TRS input with a TRS-to-DIN adapter (check the FM-1's TRS type, A or B), no computer. mk2: USB only, so through a USB host: the user's mioXL, or a computer. Both are USB devices, as the FM-1 is, so neither plugs straight in.
- **Costs.** No project format change; a settings byte or two; a CC → action table in flash. Recording: a remote knob under REC records locks on its own track, as a panel knob does on the selected one (decide: or not at all).
- **Tests.** ui_test: each row and button reaching what it should (mute, solo, morph, DJ filter, a held effect and its release), REL and PICKUP, the remote channel ignored by the LXR map, feedback after a panel knob and a kit load, NRPN morph / roll / tempo (12a).
- **To decide with the user:** the exact rows (the list above is a start); what the master column holds on each row; whether remote knobs record locks.

### Phase 13: Control all (built in 0.11)

**Built** (2026-10-25), as planned below, with these decisions (the roadmap's simpler options; to revisit after trying it):
- **The gesture:** OCT+ held + a knob on a sound page (M_SOUND, also when opened from KEYS). Not with a step key held (that's a lock), nor with a browser open. On other pages OCT+ held changes nothing: the knob edits as usual.
- **The match** (`ca_par` in ui.c): the same name (the LXR's TEXT id) in the same section on the other track's page, its n-th of that name when a page has two (the metal voices' mod waveforms); the hats' closed and open decay count as decay. Elixir's sections: the voice's own of the six. Parameters are deduplicated, so the hats' shared voice moves once. Lists (waveforms, filter types) move relatively too, clamped.
- **Relative** from each voice's own value, clamped; the knob's acceleration applies to all.
- **Keep or revert: a dialog** (changed at the user's request the same day: on the FM-1, OCT− is hard to press while the thumb holds OCT+, and a slipped thumb would keep everything). Letting go of OCT+ after a turn opens **REVERT THE KIT?** (`ui.ca_ask`), the changes still sounding: OCT+ again reverts (the default; the whole kit as at the first turn, `ca_snap`, KIT_LEN bytes in the pool), OCT− keeps. While it's open, only PLAY and REC act; knobs, keys and other buttons are ignored. OCT− while OCT+ is still held still reverts at once (no dialog then), and doesn't act as SHIFT.
- **Voice and bar keys** after the first turn: select the track / show the bar, no COPY (and a COPY source picked before the turn is dropped). Before any turn, COPY works as before.
- **Recording:** no locks; it's a kit move.
- **A kit load during the hold or the dialog** (`kit_loads`, bumped by every load into the edit buffer: a pattern's kit, MIDI CC0, a project, the library) ends it, kept, so OCT− can't put back a kit from before the load.
- **Screen:** title "CONTROL ALL: <section>" while OCT+ is held on a sound page; the steps' strip gives way to "KNOB: ALL TRACKS · TRACK KEY: COPY", then "LET GO: KEEP OR REVERT". OCT+ blinks during a Control All and its dialog.
- **Tests:** ui_test phase13_control_all (the filter on every voice from its own value, one clamped; a voice key selecting during it; release opening the dialog, knobs and keys ignored there, OCT- keeping and nothing copied; OCT+ in the dialog reverting; paging to AMP EG in the same hold, decay on every voice with both hats' decays; OCT− putting back both turns and not being SHIFT; FM moving only D1–D3; bits, a Elixir parameter, on every voice; COPY + track still working with no turn; OCT+ held on the grid editing as usual).
- **With morph (0.12, requested 2026-10-25):** the sound is the kit morphed towards the target, so Control All on the kit alone faded out with morph (nothing heard at full). Now, with morph on, the morph target's value moves by the same steps (a + d morphed to b + d is the mixture + d): heard at any amount, and it stays as morph turns. A second snapshot (`ca_msnap`, KIT_LEN in the pool) reverts it. CONFIG's 9th page, CONTROL ALL: Morph BOTH KITS (default) / KIT ONLY (`kset.cakit`). A morph target load bumps `kit_loads` too. The same could serve the panel's own knobs and the remote's, which still move the kit only (the LXR's way): to decide.
- **Not done:** which tracks (13f: skip muted, or a CONFIG setting); an absolute "all to this value" mode.

The plan as written:

Requested 2026-10-25, after Elektron's Control All: hold a button, turn a knob (filter frequency, say), and that parameter moves on every track. Keep holding and page around, and every parameter changed moves on every track. Then keep it or go back.

On Elektron boxes (Digitakt and others): hold TRK and turn a knob, and the same parameter changes on every track. NO pressed while TRK is still held puts everything back as it was before the hold. Letting go of TRK keeps the changes (the kit is the working copy; reloading the pattern reverts it). The Digitakt II can pick which tracks it applies to (CONTROL ALL CONFIG).

- **13a. The gesture: hold OCT+.** OCT+ held is COPY (with a track, a bar or a pattern), and today a knob turned while it's held edits the selected voice, as it would without it. So a knob turned while OCT+ is held, on a sound page (M_SOUND, and the sound pages opened from KEYS), is Control All. The first turn marks the hold as used, so COPY doesn't arm and nothing is copied on release. SELECT and the sound buttons still page while OCT+ is held, so you can hold it and go from FILTER to AMP EG to SHAPE. A voice key pressed during the hold is still COPY's track copy, so leave the voice keys alone (or ignore them once Control All has started: decide).
- **13b. What "the same parameter" is.** A per-voice table built at start-up from the sound pages: the knob's section, page and slot on the selected voice's page, and the same slot on each other voice's page if the label matches (filter frequency to filter frequency, decay to decay; CH's closed decay and OH's open decay are both "decay"). Elixir's own parameters are blocks of six, so it's the same offset. A voice without that parameter is skipped (FM only on D1–D3, the pitch EG on D1–SN, the model only on D1–D3). CH and OH share a voice, so it moves once.
- **13c. Relative, not absolute.** The knob's steps are added to each voice's own value, clamped to its range, so the voices keep their differences: the whole kit gets darker, it doesn't all jump to one cutoff. The knobs are encoders, so a delta is what they give already. (Elektron's behaviour here to be confirmed; an absolute "all to this value" mode could come later.)
- **13d. Keep or go back.** At the first Control All turn of a hold, snapshot every voice's sound parameters (the kit's KIT_LEN bytes, about 370 B of RAM). While OCT+ is still held, **OCT−** puts the snapshot back ("CONTROL ALL UNDONE"). That's Elektron's NO. OCT− is SHIFT, so during a Control All hold it must act as undo and not open SHIFT's combinations. Letting go of OCT+ keeps the changes ("CONTROL ALL KEPT"). As on Elektron, the kit is the working copy, and RELOAD or loading the kit reverts further. Alternative, to decide with the user: on release, ask "KEEP? OCT+ yes / OCT− no" and leave the changes audible until answered. That is closer to "yes / no" but it's one more modal state.
- **13e. On screen.** While OCT+ is held on a sound page, the title reads **CONTROL ALL** and the bottom line "OCT− undo · release keeps"; the knob's card shows the selected voice's value. OCT+'s LED blinks while a Control All hold is active.
- **13f. Which tracks.** All voices to start. Later: skip muted tracks, or a CONFIG setting like the Digitakt II's.
- **Recording.** Under REC with the sequencer running, a panel knob writes locks on the selected track. With Control All, either write locks on every track's own parameter (two lanes a step: they may run out), or write none and treat it as a kit move. Decide; the second is simpler.
- **Interactions.** Morph moves from the kit as it now is (the snapshot holds the kit, not the morph mixture). Macros and locks play over it as usual. Undo after a kit load during the hold: the load ends the hold.
- **Tests.** ui_test: filter on all voices relative and clamped, a voice without the parameter left alone, paging during the hold, OCT− reverting everything since the first turn, release keeping, COPY not arming after a Control All turn, COPY still working with no turn.


### Phase 14: LFO modes, global LFOs, page memory, Euclid's second pulse (built in 0.13)

Requested 2026-10-26, with a resource report first (in *Where things stand*). None of the four was large: together +292 B of RAM, +1.2 KB of pool, +4.5 KB of flash.

- **14a. The LFO's mode** (Elektron's FREE, TRIG, HOLD, ONE, HALF). The LXR's Retrigger already gives FREE (OFF) and TRIG, so the mode is a new per-voice Elixir parameter, `PAR_LFOMODE1` (NRPN 298–303, appended so older kits keep their layout): **TRIG** (default, the LXR's), **HOLD** (runs free, the value at each hit held: `lfo_t.hold`), **ONE** (a cycle from the hit, then still at its end: `run`, `gone`; still at the offset until the first hit), **HALF**. In HOLD, ONE and HALF, Retrigger OFF means the voice's own hits (else they'd do nothing). RANDOM draws a new value at each one-shot. On its own page, **LFO MODE**, after the LFO's two (the LFO button's third press); it locks per step, doesn't morph. KIT_LEN 371 → 377.
- **14b. Two global LFOs** (perform.c `gls`, `gl`, `glfo_block` in the audio ISR). Destinations: MORPH, FILTER (the DJ filter both ways), LP, HP, FB PUSH, COMB, CRUSH, JITTER, CUTS, PUMP. Depth −127..127. Morph and FILTER move from their knobs (Elektron-like, either way); the effects fade in from off to what their key plays at its setting, a negative depth upside down; a key held still plays in full. FB PUSH, COMB, CRUSH, CUTS, PUMP and JITTER became amounts (0..1) rather than on/off for this (`pf_amount`; `fx.push` Q16). Speed: 1/16 to 256 bars, in 16ths of the clock (`tick_q16`: MIDI clock, TAPE STOP and HALF too); at 120 BPM 256 bars is ~8.5 minutes. Modes FREE TRIG HOLD ONE HALF; triggers PLAY, PATTERN (PLAY + pattern changes), BAR (PLAY + each bar), and OCT+ on its page (`glfo_ev`, set by seq.c). Morph: the knob is `par[PAR_MORPH]`, what sounds is `morph_amt` = knob + `morph_mod` (`morph_update`; the main loop posts a new mixture at most every 8 ms). The project saves the knob. Pages: PERF, GLOBAL LFO 1 (Dest Depth Speed Wave), its MODE (Mode Trig Phase), the same for LFO 2. PERF's order, at the user's request (2026-10-26: PERFORM and PERFORM 2 split by the macros and LFOs was confusing): the performance pages PERFORM, MORE, MIX, MIX 2, PERFORM 2, its SETUP, SETUP 2 (PG_PERF2 4: their keys the second bank), the global LFOs (PG_GLFO 7), the macros (PG_MACRO 11). Saved in the project (KGL1 after KPF2; project version 9).
- **14c. Last page** (CONFIG → BUTTONS, `kset.pmem`, the settings' spare byte; default NO). A mode's page is kept when it's left (`ui.mpage`), a sound button's section and half (`gsect`, `ghalf`), and which of their modes ARP (PERF / generator) and GLO (Mix / mixer / CONFIG) showed last. HOME is left alone (it's the way to the grid).
- **14d. Euclid's second pulse** (`pattern_t.eu2[t]`: hits, rotation, join; project version 9). On the ACTION page with EUCLID (as GRIDS has its map there): **Hits 2**, **Rotate 2**, **Join** OR / XOR / AND / SUB. Hits 2 at 0 is off, whatever the join. DICE keeps the number of hits the two made. +21 B a pattern.
- **Decided without the user (to revisit):** the LFO mode on its own one-knob page; Retrigger OFF meaning "own voice" in HOLD/ONE/HALF; the global LFOs' destinations (macros left out: a macro writes the kit, so an LFO on one would rewrite the kit continuously); the effects as amounts; Euclid 2 on the ACTION page rather than a page of its own; HOME not remembered.
- **Tests:** ui_test phase14 (Euclid 2's joins, rotation, off, saved; ONE waiting, starting, stopping at the end; HALF; HOLD; TRIG free with Retrigger off; the LFO MODE page; a global one-shot saw morphing over a bar from PLAY, the knob unmoved, from the knob, negative depth; the speed at 1 and 256 bars; FILTER both ways; COMB and FB PUSH by amount; HOLD per BAR; the pages, OCT+; saved with the project; Last page for FX, GLO→CONFIG, LFO, ARP, and off). library_test: KIT_LEN 377.
- **Next:** the user tries it in the emulator, then on the unit (CPU: the two LFOs are a few multiplies a block; a fast LFO on MORPH re-posts the whole kit every 8 ms, as turning the morph knob does).

### Phase 15: Elixir — getting ready to share it (done in 0.14)

Requested 2026-10-26: the project gets a new name and a front page, ready for other people.

- **15a. The name: Elixir.** Kettle becomes Elixir everywhere: the screen, the USB name, the docs, the site, the file names (`elixir.c`, `elixir.fwsc`, `elixir.wasm`, `ELIXIR_VERSION`, `ELIXIR_KITS_DIR`). Written plain, "Elixir" (ELIXIR on the screen): the user first asked for the stylization eLiXiR, but its capitals spell LXR, a trademark Erica Synths' terms keep out of a firmware's name (see *Phases 6–10*, Name); the user chose plain Elixir (2026-10-26). Kettle is mentioned as Elixir's former name where people may still meet it (the install and library pages, the README, the manual's upgrading note).
- **What keeps its old bytes, on purpose** (changing them would lose people's data or break the pages against units still running Kettle): the flash magics ("KPRJ", "KET1", "KETL", "KL"), the library's SysEx header (7D 'K' 'T'), the backup files' magics and extensions (`.ktbk`, `.ktp`, `.ktlb`), the package identity (FM-1_900), and the emulator's saved flash in the browser (read from the old key if the new one is empty).
- **15b. The title screen:** an 8-bit potion (a flask drawn in 5 px squares, the liquid in the palette's theme colour), ELIXIR in pixel letters with a shadow, "drum machine" below, the version under that (`ui_draw.c` `POTION`, `WORD`, `draw_splash`). Shown 1.2 s (was 0.6 s), so it can be seen. The site draws the same map as SVG (`make_site.py` reads it from `ui_draw.c`).
- **15c. Upgrading from Kettle — the step that mustn't brick a unit.** The updater's identity check doesn't look at the name: the FM-1 answers "FM-1_900" whether Kettle or Elixir runs, the loader "ota-FM-1_900". What does look at names is the port filter: the pages and `fm1_install.py` only talk to MIDI ports named like an FM-1. A unit running Kettle is a port named "Kettle"; after the update it's "Elixir" (or still "Kettle" where the computer's MIDI system remembers the old name). So:
  - the install page, the library page, `fm1ota.js` and `fm1_install.py` accept both names, for the whole install (before, the loader, after);
  - the install page's device check says a Kettle unit is ready to update to Elixir (projects kept), not "an alternative firmware";
  - `tests/install_test.mjs` runs the install page's updater against simulated units: Kettle → loader → Elixir, Elixir → Elixir, the name kept as "Kettle" after the update, a write cut off and resumed, V15 → Elixir; and refuses other gear.
  - Projects and settings: Elixir reads Kettle's: the rename changed names, comments, the splash and the USB strings, not a format, a magic or a flash address (the image still ends below the projects; the loader's write range is Felucca's, unchanged).
  - Not yet done on hardware: the user's own Kettle 0.13 unit updated from the new page (the first real run of this path).
- **15d. The repository:** github.com/dmajka/kettle → github.com/dmajka/elixir. The user renames it on GitHub (Settings → General → Repository name); then `git remote set-url origin https://github.com/dmajka/elixir.git`. GitHub redirects the old repository URL, but not the old Pages site: dmajka.github.io/kettle/ stops working.
- **15e. The front page:** the site's home page becomes a page about Elixir (what it is, what it adds to the LXR-02's engine, try it in the browser, install), with the install steps on their own page (`install/`). A pixel-potion theme for the art only; the text plain and easy to read. Built: `web/index.html` and `web/site.css` (shared by the front, install and library pages; light and dark), real screens from ui_test (`tools/site_shots.py` → `web/shots/`), a header on every page. Checked in Chrome, desktop and phone width.
- **Decided without the user (to revisit):** the backup files keep `.ktbk`/`.ktp`/`.ktlb`; the version is 0.14 (not 1.0); the splash's length; the front page's wording and which screens it shows.

### Phase 16 (built 2026-10-26, released in 0.15): Plaits' engines as models

**Released in 0.15 (2026-10-27), with Phase 17's second core; the status below is the prototype's (2026-10-26).**
- All 16 original engines are ported to fixed point: `firmware/src/plaits.c` (helpers, the percussion engines, the voice glue) and `plaits_va.c` (the pitched engines), tables `plaits_tables.h` (`tools/gen_plaits_tables.py`). They are MODEL entries 4–19 (`P VA` … `P HH`) on D1–D3; the knobs read Harmonic, Timbre, Morph / Colour, Aux, Env on those voices. No kit format change.
- `tests/plaits_compare.sh` (with Plaits and stmlib checkouts) holds each engine to Plaits' float code: 5 settings × 4 pitches each. BD, SD, string, particle, additive and wavetable match sample by sample (correlation ≥ 0.98). The others are checked by level (±1.5 dB) because their detail drifts apart over time (noise clocks, beating chords, FM feedback, float phase rounding). Known gaps: two MODAL cases at full timbre and morph are 1.9 dB low; NOISE at n72 with full timbre is 1.9 dB off.
- Departures from Plaits: one string per voice (Plaits has three); one shared particle diffuser, owned by the first voice on PARTICLE (the others play dry); speech words are decoded from the bitstream as they play, not loaded (14 KB a voice saved), and a word bank is silent until its first hit after a bank change (Plaits scans its frames); no prosody or speed; COLOUR 0 is a plain VCA.
- Build: image 435,840 B; RAM .data+.bss 61,152 of 73,728 B (the 16 KB diffuser is in fast RAM); pool unchanged. CPU not measured yet (the emulator shows none): that is the next step on hardware, worst case three voices on the heaviest engines (chords, speech, additive, swarm).
- render_test renders each engine on D1 (all sound, none clip); ui_test passes unchanged. MANUAL: §9's Plaits section in 0.15.
- **First hardware load test (2026-10-26, the user):** a Plaits engine on one track with other sounds, 65–76%; two models on D1/D2 (Plaits + Plaits, or Plaits + Rings) with delay, reverb and a global LFO on morph, peaks over 110% and the sound breaks up; the same without morphing, 75%, peaks 75–82%.
  - **Cause of the morph overload (not Plaits itself):** every morph step (each 8 ms with a global LFO on morph) re-applied all 377 kit parameters inside one audio block, about as much work as a whole block of a busy kit (host: ~4 µs against 6–10 µs). Before Plaits the kit's headroom hid it. Fixed: morph now refreshes as a sweep, 48 parameters a block over 8 blocks (under 6 ms), and only those the kit and the target differ in (`sound.c snd_morph_sweep`); a lock is found through a bitmap (`auto_lk`) rather than a search of every lock for every parameter. A morph step's block now costs ≤ 0.4 µs more on the host (≤ 7% of a block). Kit loads still refresh at once. To re-measure on hardware.
  - **The second core** (D1–D3, or the models, rendered on core 2) is the real headroom, and a Phase of its own. The AC79's second core misreads memory now and then on some units (found by the Melodee and fm1-x0x developers); JIANT (github.com/juanjiant-bit/JIANT, `firmware/hal/fm1_multicore.h`, from 0.6.71) runs it guarded: a check word on each job, a deadline, falling back to one core for the rest of the power-on on a bad read, no answer or a fault; output range checks on the effects; REC at power-on for one core; the reason under ABOUT. Melodee and fm1-x0x are GPL-3.0 as Elixir, so their method can be reused with credit. Elixir's build checker currently asserts "one core: no second-core code".

Requested 2026-10-26 as research only: how feasible is Mutable Instruments **Plaits** as another voice, given the resources and the interface? The user's view: not as a new track, because the FM-1's sequencer screen is already full. Read from github.com/pichenettes/eurorack at f4cfa76 (2026-10-12), `plaits/`. Not built.

**What Plaits is, in code terms**
- 24 engines in three banks (the 1.2 firmware): **VA VCF, phase distortion, 6-op FM ×3 (DX7 banks), wave terrain, string machine, chiptune**; **VA, waveshaping, 2-op FM, grain/formant, additive, wavetable, chords, speech**; **swarm, filtered noise, particle noise, inharmonic string, modal resonator, analog bass drum, analog snare, analog hi-hat**. One engine plays at a time.
- A voice's controls (`dsp/voice.h`, `Patch`): engine, note, **harmonics, timbre, morph**, the **LPG's decay and colour**, and how much the internal decay envelope moves FM, timbre and morph (the attenuverters' job when the trigger is patched and the CV isn't). Two outputs, OUT and AUX (AUX is a variant: a sub, the other half of a stereo pair, a raw oscillator…). A trigger strikes the low-pass gate; without one the voice drones.
- The DSP is float throughout, at 48 kHz in blocks of 12 (`kMaxBlockSize` 24), on a 72 MHz Cortex-M4 with an FPU. About 16,000 lines of DSP (`engine` 3.4k, `engine2` 1.5k, `speech` 3.1k, `oscillator` 2.4k, `fm` 2.0k, `drums` 1.1k, `physical_modelling` 1.0k, `fx` 0.9k, the rest smaller), against the ~3,000 lines of Rings that Phase 10 ported.
- **Licence: MIT**, as Rings. Its notice joins `LICENSES/` and NOTICE.md; GPL-3.0 for the whole is fine.

**Where it goes: as models, not as a track.** The MODEL section (EDIT, after the transient) already turns a drum voice into something else (Rings' MODAL, STRING, FM), struck by the voice's hits. Plaits' engines join that menu. Then:
- **No new track, no change to the sequencer screen.** The track keeps its steps, locks, conditions, ratchets, generators, KEYS, filter, shape, sends, random per hit and cross mod. A new (8th) track would need a black key (all eleven are taken: seven tracks, four bars) and ~23 KB of pool for its steps over 32 patterns, where ~13 KB is usable.
- **No kit format change.** The model parameters are already blocks of six (`PAR_MODEL1` … in sound.c) with seven knobs: Model, Decay, Bright, Struct, Position, Exciter, Tension. Plaits needs seven too: **Model, Harmonics, Timbre, Morph** on the first row, **Decay** (the LPG's), **Colour**, **Aux** (OUT to AUX) and **Env** (the internal envelope's amount on timbre and morph, as one knob) on the second. The same bytes, named per model on screen (as Elektron's machines do). Only the Model list grows (`NMODEL`), so kits stay 377 B and the library's message (390 of LIB_MAX 400) doesn't move. Kits saved with a Plaits model play LXR on older versions (sound.c already reads a model number past `NMODEL` as 0).
- **Locks for free.** Every model knob locks per step already, so a timbre or morph lock per step, the way Plaits is usually sequenced with CV, needs nothing new. Macros and the global LFOs' kit destinations reach them the same way.
- **Pitch** from the voice's Coarse/Fine, the step's note, the pitch EG and KEYS, as the Rings models take it. Velocity strikes the LPG harder.
- **Which voices.** D1–D3 first, as Rings. The kit already holds model bytes for SN, CY and HH (unused); opening the MODEL page on them is UI work, but their memory isn't there (below).

**Resources** (estimates from reading the code; nothing measured)
- **RAM.** Plaits gives all its engines one 16 KB buffer (`shared_buffer`, re-initialised on an engine change), and the largest single user is the particle engine's diffuser (8,192 × 16 bit = 16 KB). The others need far less: the inharmonic string 1,024 + 256 samples (5 KB at 32 bit), the string machine's ensemble 1,024 samples (4 KB), the six-op FM's two voices and a bank of 32 patches (~4 KB), the rest a few hundred bytes. A drum voice plays Rings **or** Plaits, so a voice's Plaits state can share the memory its Rings lines already take (`rg_line` + `rg_stretch`: 6 KB per drum voice in fast RAM). With the particle diffuser shortened to fit (or its reverb taken by Elixir's own), **D1–D3 need little new RAM**: the engines' small states, perhaps 1–2 KB. SN, CY and HH would need ~6 KB each, which the pool (13 KB usable) has room for once, not three times.
- **Flash** (~244 KB usable). The tables: the wavetables (`wav_integrated_waves`, 25,344 × 16 bit = 50 KB), the DX7 banks 12 KB, the speech engine's LPC words ~14 KB, the rest a few KB. The code, in fixed point, maybe 60–100 KB for all 24. All of Plaits might just fit, and leave little for anything after; a chosen set is safer.
- **CPU.** On a real FM-1, 0.5 measured ~60% average, 64% peak; the Rings models (0.9) are still unmeasured, and that's the first step here too. Plaits' engines by weight (reading the code): **light** VA, waveshaping, 2-op FM, the three analog drums, filtered noise, chiptune, phase distortion (about an LXR voice each); **medium** wavetable, wave terrain, grain/formant, inharmonic string, modal, particle, VA VCF; **heavy** chords, additive (36 harmonics), swarm (8 voices), speech, six-op FM (12 operators a sample), string machine. Three heavy engines at once could pass the budget; a cap (say, one heavy engine, else the model falls back to its light form), or the second core (`FELUCCA_CPU2`, see *0.5.1*) for the models, are the ways out.
- **Fixed point.** The FM-1 has no FPU, so each engine is rewritten as Rings was (no float, 32-bit divisors, Q formats with headroom where feedback needs it), at 44.1 kHz rather than 48 (`kCorrectedSampleRate` and the tables retuned), in Elixir's 64-sample blocks (run as 64, or as Plaits' 12-sample steps where its parameter interpolation needs them). `rings.c` already has the stmlib pieces (SVF, pitch steps, the string and resonator: Plaits' inharmonic string and modal engines are close relatives of Rings').

**Steps, if chosen**
- **16a. Measure first.** CPU on the unit with three drums on MODAL (owed since 0.9), so the budget is known.
- **16b. The frame:** the voice around the engines (`voice.cc`): note and parameter handling, the decay envelope and its amounts, the LPG (decay, colour) and its bypass for engines that envelope themselves, the AUX blend. The MODEL page's per-model labels. `tests/plaits_compare` against Plaits' float code (`EURORACK=…` as `rings_compare.sh`), same thresholds (correlation 0.99, −30 dB).
- **16c. First set: the drum-friendly, light engines.** Analog bass drum, snare and hi-hat (with their synthetic variants on AUX), VA, waveshaping, 2-op FM, filtered noise, particle (shortened), inharmonic string and modal. Small tables; no 50 KB wavetable yet. About Phase 10's size twice over.
- **16d. Second set:** wavetable, grain/formant, chords, additive, swarm, speech: the flash and the heavy CPU.
- **16e. Third set (the 1.2 bank):** VA VCF, phase distortion, six-op FM (the three built-in banks; loading your own DX7 banks over SysEx later, through the library), wave terrain, string machine, chiptune.
- Each set: ui_test (pitch, each knob moving the sound, locks, a project with the model), compare test, the CPU meter on the unit.

**To decide with the user:** which engines (all 24, or a set: the drums and a few tonal ones); D1–D3 only, or SN/CY/HH too; the LPG on (Plaits' character) or the voice's own amp EG; a CPU cap or the second core; whether the model menu names them with a P: prefix (P:VA, P:CHRD, P:SPCH …) after LXR, MODAL, STRING, FM.

**Decided with the user (2026-10-26)**
- **The 16 original engines**, the ones Plaits shipped with in 2018: eight pitched (VA, waveshaping, 2-op FM, grain/formant, additive, wavetable, chords, speech) and eight noise and percussion (swarm, filtered noise, particle, inharmonic string, modal, bass drum, snare, hi-hat). The 1.2 firmware's third bank (VA VCF, phase distortion, six-op FM, wave terrain, string machine, chiptune: 24 models, 22 engines) is left for later. Steps 16c and 16d cover the 16; 16e is the later option.
- **D1–D3 only.**
- **The voice's own amp EG** (proposed; the user is open to the LPG). Plaits' percussion engines (inharmonic string, modal, the three drums) envelope themselves and bypass the LPG anyway (`already_enveloped`), so it only matters for the pitched engines, noise, particle and swarm. There the LPG ties brightness to level, as a vactrol does: a plain VCA plus the voice's filter is close but not the same. The LPG is cheap (one filter and a gain a sample), so it's ported, driven by the amp EG instead of Plaits' own ping envelope: **Colour** 0 is a plain VCA (the default), higher closes the LPG's filter with the amp EG. One envelope, the ENV pages as on every other model, and the vactrol sound when wanted. The amp EG's decay also drives Plaits' internal envelope (the **Env** knob's amount on timbre and morph). The second row is then **Colour, Aux, Env** (three of the seven bytes; Plaits' own decay knob isn't needed).
- **Built for one core, all three tracks, no cap**, then measured on the unit before deciding to keep it, cap it or move it to the second core (16f). The models' rendering is written as one call per voice from the start, so moving it is a small change.

**Sizes, engine by engine** (2026-10-26). Code: Plaits' float code compiled with clang -Os for arm64, each engine with the oscillators and helpers it inlines (a proxy: the fixed-point code on the pi32v2 may be 1–1.5 times this). Tables: from `resources.cc` and the speech files. RAM: each engine's buffer from the allocator; a voice's buffer can sit in the 6 KB its Rings lines already take (it plays Rings or Plaits, not both). State: the engine's own members, a few hundred bytes each, ~1.5 KB a voice for the largest kept as a union.

| Engine | Code | Tables | RAM buffer a voice | CPU (by reading) |
|---|---|---|---|---|
| VA | 2.8 KB | – | 0.1 KB | light |
| Waveshaping | 1.5 KB | 2.6 KB (5 shapers) + 4 KB (folds) | 0.1 KB | light |
| 2-op FM | 0.9 KB | (sine, shared) | – | light |
| Grain / formant | 3.0 KB | – | – | medium |
| Additive | 1.9 KB | – | 0.2 KB | heavy (36 harmonics) |
| Wavetable | 1.9 KB | **50.7 KB** (`wav_integrated_waves`) | 0.2 KB | medium |
| Chords | 2.9 KB | – | 0.2 KB | heavy |
| Speech | 6.5 KB (3 synths) | **11.1 KB** (LPC words) + 0.9 KB | ~1 KB | heavy |
| Swarm | 1.6 KB | – | ~0.5 KB | heavy (8 voices) |
| Filtered noise | 1.4 KB | – | 0.1 KB | light |
| Particle | 2.0 KB | – | **16 KB** (its diffuser, 8,093 samples × 16 bit) | medium |
| Inharmonic string | 3.9 KB | 0.3 KB | 5 KB (1,024 + 256 samples × 32 bit) | medium |
| Modal | 2.3 KB | – | 0.1 KB | medium |
| Bass drum | 3.2 KB | – | – | light |
| Snare | 2.9 KB | – | – | light |
| Hi-hat | 3.7 KB | – | 0.2 KB | light |
| Shared (sine, SVF, stiffness, downsampler) | – | ~4 KB | | |
| **All 16** | **~42 KB** (fixed point: ~45–65 KB) | **~74 KB** | | |

- **Flash: all 16 fit.** ~120–140 KB of the ~244 KB usable, leaving ~100 KB. Wavetable is the one large item (51 KB); without it, ~70–90 KB. Some tables Elixir already has (sine, `rings_tables.h`'s SVF and stiffness) are shared.
- **RAM: all fit but particle.** Its diffuser (the reverb-like smear at high timbre) is 16 KB a voice; three would be 48 KB, and fast RAM has ~27 KB usable. Options: (a) **one diffuser**, ~16 KB of fast RAM, for the first of D1–D3 that is on PARTICLE; the others play it dry; (b) the diffuser in 8 bits, 8 KB a voice: one in the 6 KB Rings slot doesn't fit, two would need ~16 KB more; (c) shorter delays (3 × ~2 KB in the Rings slots) and a smaller smear. (a) is proposed: the sound is kept where it plays, and it's the only engine over its slot.
- **CPU:** not knowable before 16a; light engines are about an LXR voice each, heavy ones several.

**16f. The second core, if the measurement asks for it.** (2026-10-26: it does. Now **Phase 17**, below; this is the first sketch.) `firmware/hal/fm1_cpu1.h` is already in the tree (from X0X 1.0.5, hardened by Melodee's and Jangada's work): start, a one-job mailbox, a check word, and a fall back to one core on units whose core 1 misreads RAM. `FELUCCA_CPU2` is 0 today. The work:
- Turn it on: `FELUCCA_CPU2 1`, start core 1 at boot, a CONFIG line showing whether it runs (and its stack use, `fm1_cpu1_stack_used`).
- In the audio ISR: hand core 1 "render D1–D3" (the voices and their models, so the Rings EXCITER's input stays with its voice), render SN, CY, HH on core 0, wait, then the cross mod, the mixer and the FX as now. DUCK and RING read the sources' blocks after the wait; FILTER already uses the previous block.
- What both cores could touch: the voices' noise generator (`noise_rng` in dsp.c, one xorshift for every voice) becomes one per core; the sequencer's (`seq_rand`) runs before the hand-over and stays as it is; kit posts from the main loop already land between blocks; the voice structs are each one core's during a block.
- The code is small (under an hour of a session: the HAL does the hard part). The time is on the unit: the emulator models neither CPU time nor core 1, so the test is the CPU meter, the late buffers, and core 1 left running a busy pattern for hours (the misreads come under load, now and then), plus an install-and-check round trip for each fix.
- **The catch:** a unit whose core 1 misreads falls back to one core and then runs late if the models need two. So even with the second core, the one-core path needs a way down when the load is too high, e.g. the heavy engines (additive, chords, speech, swarm) refused on a second and third voice. Decided after 16a.

**Steps, revised:** 16a measure (Rings on three drums); 16b the frame (the amp EG driving the LPG and Plaits' internal envelope, AUX, the labels, `plaits_compare`); 16c the eight percussion and noise engines, particle's shared diffuser; 16d the eight pitched engines; then on the unit with three heavy engines at once, and 16f if needed. 16e (the 1.2 bank) later, if wanted.

### Phase 17 (17a–17c released in 0.15, 2026-10-27): the second core

**First hardware round (the user, 0.15-test1, 2026-10-27).** Init kit, even hits on every track, no delay: CPU 39%, peak 45%; D1-D3 18%, SN-HH 18%, MIXER 4% (a plain LXR voice ~6%). One model on D1, D1-D3 becomes: Rings MODAL 42/44% (peak), STRING 27/30, FM 24/25; P VA 33/36, P ADDTV 38/40, P NOISE 39/41, P PARTCL 38/39. Two MODALs (D1, D2): 55/57 (24 modes each, modal_budget). MODAL + FM: 49/52. MODAL + P VA: total over 100%, the sound breaks up (no crash). Delay 3%, reverb 4/5% plus ~5% more under MIXER (the reverb sends, mix_voice). Core 1 with the test job: CORES 2, JOB 144 us, MISREADS 0; core 0's CPU 42%/44% with it, 41%/42% with REC held at power-on: the cores barely slow each other.
- **Decided (the user):** D1-D3 on core 1 (the fixed split), and at most two models at once: the user first suggested models on D2/D3 only, then chose the cap (P BD belongs on D1). Remove voices only if two cores aren't enough.

**17c, built in 0.15-test2 (2026-10-27).**
- `voices_render(lo, hi)` (voices.c) renders voices into `vbuf`; each block core 0 hands core 1 `c1_drums` (D1-D3), renders SN-HH, waits, then the mixer, FX and reverb as before. A failed hand-over or wait (the HAL holds core 1) renders D1-D3 on core 0 in the same block. The test job is gone.
- What both cores touched: the voices' noise (one `noise_rng` for all) is now one xorshift per oscillator (`osc_t.rng`, seeded in `voices_init`) and one for Rings' models (`rg_rng`); `noise_rng` is left to the sequencer and the LFOs (core 0, before the hand-over). Plaits' `pl_rng`, the word scan and the particle diffuser are D1-D3's only, so core 1's. The voices' noise sequences changed (compare_test's noise cases were already uncorrelated by design).
- **The models' limit:** `mdl[].want` is the kit's model (sound.c), `mdl[].type` the one that plays: `model_limit` (rings.c), each block and at each change, lets the first `model_cap` (2; 1 on one core, main.c) in D1, D2, D3 order play and the rest play as LXR. The MODEL page says OVER 2 MODELS: PLAYS AS LXR (or 1 MODEL ON ONE CORE). `model_reset` keeps 8 bytes now (the knobs and want; render_test caught it wiping `want`).
- LOAD: with two cores D1-D3 is core 1's time (`c1_ticks`); `LD_WAIT`, core 0's wait for it, and core 1's stack (`fm1_cpu1_stack_used`, red over 3.5 KB) on the cores line. Host estimate of core 1's deepest stack (drum_block, model_block, modal_run): ~1.5 KB of 4 KB.
- Tests: render_test renders a beat (MODAL on D1, P NOISE on D2, D3 on noise) in two processes, D1-D3 first and SN-HH first, and requires the same output bit for bit (it fails with the old shared noise); and the limit (a third model plays LXR; D3's plays when D1's goes; one core, one). ui_test 96 screens (the MODEL page over the limit). compare_test, install_test, library_test pass.
- Cost: RAM .data+.bss 66,004 B; pool back to 347,268 B (no test table); image 439,504 B.
- **On the unit (next):** the same kits as the first round: CPU, D1-D3 (core 1), WAIT, STACK; two heavy models (MODAL + P VA, P ADDTV + P NOISE) with delay and reverb, morphing; the soak (hours, CORES 2); REC held: one model plays, CPU as before. Then 17d (the effects a block behind on core 1) only if core 0 is the slower half.

**Second hardware round (the user, 0.15-test2, 2026-10-27).** Stock kits: CPU ~27%, down from ~45% on one core. The most on core 1: D1-D3 62%, peak 71%, with two Plaits/Rings models and morph; CPU never over 100%. A meter bug: REVERB up to 4088% (peak) while morphing, the sound fine. `load_clock` read TIMER4 and TIMER5's nested ticks apart, so a TIMER5 tick between them stepped the clock back and a short part wrapped below 0. Fixed in 0.15-test3: the two reread until TIMER5 didn't come between, a negative step counts 0, a part at most 4 halves.

**Third round (the user, 0.15-test3, 2026-10-27).** No major problems; two models play without overload. But now and then, turning MODEL through the Plaits engines, the second core switched off (the MODEL page's "1 MODEL ON ONE CORE"). Cause: Plaits' speech word scan (`pl_words_scan`), done the first time SPEECH played after power-on, inside a block: 62 us on the host, ~12 blocks' worth, so ~7 ms on the FM-1, past the 4 ms deadline (`fm1_cpu1_wait`): NO ANSWER, one core. Fixed in 0.15-test4: the scan runs at boot (`engine_init`), as 17c's plan had it; render_test checks it. The other engines' first blocks after a switch (host, second visits): no one-off cost. Core 1's deepest stack from the firmware's disassembly: ~2.5 KB of the 3.8 KB the stack limit allows (voices_render's frame 1.6 KB, everything inlined).

**17a and 17b, built in 0.15-test1 (2026-10-26; measured above).** (The test job below was replaced by D1-D3 in test2.)
- **17a, the load by part:** CONFIG → LOAD (the last CONFIG page, no knobs): D1-D3, SN-HH, MIXER (cross mod, choke, the mixer, the master and the ISR's output scaling), FX BUS, REVERB, SEQ, MOD (the rest: parameters, MIDI, PERF, the sequencer, modulation), each as a share of the half buffer (2.9 ms, as the CPU line), average (32 halves, as `cpu_q8`) and peak since CONFIG opened, with a bar. `LOAD_MARK` (dsp.c) at the part boundaries in `voices_block` and `engine_block`, TIMER4 less TIMER5's nested ticks; the host and the emulator count nothing (no `LOAD_CLOCK`).
- **17b, core 1 on, guarded, a test job:** `FELUCCA_CPU2 1` (elixir.c, build.py `CPU2 = True`). `cores_boot` (main.c) starts it before the audio and the timers; each half the audio ISR hands it `c1_test_job` (4 passes of an order-sensitive sum over a 4 KB table in the pool, filled at boot; core 0 knows the answer), renders as before, then waits. A wrong sum counts as a **MISREAD** (shown; the core is kept, so the soak test counts them). The HAL's guard (check word, 4 ms deadline) holds core 1 at the first bad job or missed deadline: one core until power-off, "SECOND CORE OFF: CONFIG". LOAD shows `CORES 2  JOB n us  MISREADS n`, or `CORES 1:` and the reason (REC HELD AT POWER-ON, BAD READ, NO ANSWER, FAULT, SINCE A CRASH, NO START).
  - REC held at power-on: core 1 is held ~50 ms into the splash (it has to start before the timers, and the keys are read only after), "ONE CORE (REC HELD)".
  - Crash-loop guard: `bootguard.c2` (`.noinit`, magic changed to 0x42475245 as the struct grew) is 1 while two cores run; a crash (`fm1_fault`) makes it 2. At the next start, 2, or 1 after a watchdog reset, or `bootguard.failed`: that session is one core (SINCE A CRASH), the one after tries two again.
  - `fm1_cstart` holds core 1 first and copies `.c1_text`, as Felucca's.
- **Cost:** RAM .data+.bss 65,908 B (+4.8 KB: core 1's stacks 4.3 KB, the mailbox); pool +4 KB (the test table); image 439,344 B.
- **Tests:** ui_test 95 screens (LOAD with one core, two, and a bad read; the parts' peaks cleared when CONFIG opens; BUTTONS is now the second-last page); render_test, install_test, library_test pass; the emulator builds.
- **On the unit (the next step):**
  1. CONFIG → LOAD with plain kits, then with two and three models (Plaits and Rings), delay and reverb on, morphing: note each part's average and peak. That decides 17c (fixed or measured split) and 17d (the effects a block behind) and the governor's order (17f).
  2. Compare the CPU line with core 1 on and with REC held at power-on: if core 0's load rises with core 1 running its job, the cores slow each other (the shared flash cache, the bus).
  3. The soak: a busy pattern for hours; CORES should stay 2 and MISREADS 0. Note JOB (us).
  4. Updates, UBOOT and a crash still work with core 1 running (the reset paths hold it first).

**Why.** The first hardware test of Phase 16 (above): with the morph fix, two models on D1/D2 morph cleanly, but delay and/or reverb on top still pass 100%. The FM-1's AC791N has two cores and Elixir uses one. The goal is headroom: the models and the effects together, and room left to improve the reverb or delay. One firmware for every unit, no separate one-core build.

**What is known about the hardware**
- **The driver is already in the tree:** `firmware/hal/fm1_cpu1.h` (Felucca's, from X0X 1.0.5 by Charles Vestal, with Melodee's and Jangada's findings; all GPL-3.0). It covers starting core 1 through the chip's ROM, a one-job mailbox with each core's writes on its own cache lines, a check word on each job, a 4 ms deadline, holding core 1 again, and its stack use. The HAL also holds core 1 before every reset path (`fm1_sys.h`), with the bus traps handled, which fixed Felucca's "1 in 6 updates crashed with 2ND CORE ON". Today `FELUCCA_CPU2 0`, and build.py checks that no second-core code is linked; that check becomes "second core built in".
- **Units that misread.** On some FM-1s, core 1 now and then reads a word of RAM wrong, more often under load (fm1-x0x#10, measured by Jangada; Melodee 1.0.1). The guard, which every firmware that ships two cores now uses (Melodee, JIANT, X0X):
  - Core 1 runs a job only when its check word matches.
  - A bad read, a missed deadline or a fault holds core 1, and core 0 plays everything, losing at most one block.
  - That unit then stays on one core until power-off.
  - JIANT also range-checks the effects' output: far out of range, the block is muted, the lines cleared and core 1 stopped.
- **The clock.** The FM-1's boot loader sets 360 MHz, and Elixir never touches it.
  - The claim the user read is that the chip is rated for 320 MHz and M-VAVE runs it at 360. No public JieLi datasheet confirms or denies that.
  - What is on record (Melodee's notes): 1.3.0 retuned the clock to 320 MHz at every start. On some units, often the ones with second-core trouble, that retune failed, and two failed starts in a row put the FM-1 in UBOOT. 1.3.2 went back to "never touch the clock", and 1.3.4 removed the 320 MHz option.
  - So the risky step is changing the clock, not running at 360. The second-core trouble and the failed retunes may come from marginal parts, which the guard above handles.
  - Elixir keeps 360 MHz and never changes the PLL. Dropping to 320 MHz would also cost 11% of the speed this phase is meant to win.
- **One package.** Melodee first shipped standard and single-core packages, then retired the single-core one once its guard was in (its installer offers dual-core only). JIANT ships one. Elixir does the same: one `.fwsc`, with the core count decided at run time.

**Where the time goes** (host profile, 2026-10-26: a 7-track pattern with every voice playing; host microseconds, so only the shares carry over, and they must be re-measured on the unit in 17a)

| Kit | Block | D1–D3 | SN, CY, HH | FX bus (delay) | Reverb | Rest |
|---|---|---|---|---|---|---|
| Plain LXR voices, no effects | 4.7 µs | 55% | 41% | 1% | 0% | 3% |
| Plain, delay + reverb | 5.0 µs | 49% | 37% | 3% | 6% | 5% |
| Plaits PARTCL + STRING, delay + reverb | 6.6 µs | 61% | 28% | 3% | 5% | 3% |
| Plaits PARTCL + Rings MODAL, delay + reverb | 11.3 µs | 77% | 17% | 1% | 3% | 2% |
| Plaits ADDTV + SWARM, delay + hall | 9.5 µs | 73% | 20% | 2% | 3% | 2% |

On the host the effects are cheap. On the FM-1 the user saw them push a 75–82% kit over 100%, so the FM-1's costs differ: its memory (the 64 KB delay line and the reverb's combs are in the pool) and its 64-bit multiplies. That is why 17a comes first.

**Steps**
- **17a. Measure the parts on the unit (one core).** A load breakdown on CONFIG: D1–D3, SN–HH, the mixer and cross mod, the FX bus, the reverb, and the sequencer and the rest, each as a share of the 0.73 ms block (TIMER4, as the CPU line). Small, no risk, and it decides 17c and 17d.
- **17b. Core 1 on, guarded, doing nothing yet.**
  - Started at boot.
  - ABOUT / CONFIG shows `CORES 2`, or `CORES 1` with the reason: HELD AT POWER-ON, BAD READ, NO ANSWER, FAULT, SINCE A CRASH or NO START.
  - Holding a key at power-on (to choose) runs that session on one core.
  - Crash-loop guard (Melodee's): a reset while two cores ran makes the next start one core (the `.noinit` bootguard word survives a reset).
  - A soak test: hours of a busy pattern with core 1 running an idle job every block, counting bad reads.
- **17c. Split the voices between the cores.** Two ways:
  - **Fixed: D1–D3 on core 1** (the user's suggestion), the rest on core 0. Simple, and with plain voices nearly even (about 49/51). With two heavy models, core 1 has three quarters of the work, so a block finishes only about 25% sooner.
  - **Measured (JIANT's way, recommended):** each voice's render time over the last blocks. Each block, the voices are split greedily so the two cores finish together; a voice is never split. About the same code, and good balance whatever the kit: plain, one model, three models.
  - **Either way:**
    - Core 0 hands core 1 its voices, renders its own, and waits.
    - Then, as today on core 0: cross mod, choke, ducking, the mixer, the FX and the master.
    - Shared state is made per voice:
      - the voices' noise generator (`noise_rng`, dsp.c);
      - Plaits' word scan (`pl_words_scan`, moved to boot so two speech voices can't race);
      - the particle diffuser's owner (already one voice).
    - A host test plays a whole song one-core and split several ways, and checks the output is identical, bit for bit (JIANT has the same test). The voices' outputs then don't depend on which core rendered them.
- **17d. The effects on core 1, a block behind** (JIANT 0.6.75). The FX bus and the reverb process the previous block's sends while core 0 renders the voices. Only the wet signal is 0.73 ms late, which is inaudible; the dry sound is unchanged. With 17c this lets the effects overlap the voices instead of waiting after them. It also needs an output range check (mute, clear the lines, one core).
- **17e. Use the room** (only after 17c and 17d are measured, and not beyond what one core can still play, 17f). Candidates:
  - a better reverb (a denser plate or FDN; the hall's tail);
  - a stereo or modulated delay;
  - the second FX bus (0.5.1's idea);
  - the heaviest Plaits engines on all three voices;
  - the 1.2 bank (16e: six-op FM, the string machine…).
- **17f. The one-core path stays playable.** A unit that falls back must not crackle.
  - The CPU line shows when a block ran late.
  - A governor sheds load in a fixed order when one core can't keep up. The order is decided after 17a. Candidates: the reverb to a cheaper form, the heavy Plaits engines at reduced quality (fewer chord voices, fewer additive harmonics), a second heavy engine refused.
  - The manual says which units this applies to and how to check (ABOUT).

**Risks and unknowns**
- **Flash.** Both cores fetch code from the same flash cache. The speed-up may be well under 2×; 17b's idle job and 17c's first split will show it.
- **Flash erases** (saves) still stop everything. The output already fades around them.
- **Misreads.** Only a soak test shows how often a given unit misreads. The guard limits the cost to one block of glitch and one core afterwards.
- **The emulator** models neither CPU time nor core 1. It can run the split sequentially, which checks the logic and the bit-identical output but not the timing, so the timing tests happen on the unit, with an install round trip per fix.

**Effort.** 17a and 17b: a session plus hardware rounds. 17c: a session (the HAL does the hard part). 17d: a session. 17e: open-ended.

**Credits.** Starting core 1 safely and the misread guard: X0X (Charles Vestal), Melodee (keremimo), Jangada (zednaked), all GPL-3.0, already credited in `fm1_cpu1.h`. The split and the effects-a-block-behind approach: JIANT (juanjiant-bit), credited in the code and NOTICE.md when used.

### Phases 18–23 (research, 2026-10-27): scales, melody, density, a third LFO, better delay, reverb and dirt

Asked 2026-10-27, as research: what each idea would cost on the FM-1 (CPU, RAM, flash, project and kit space) and in what order to build them. Nothing here is built yet. Most of it belongs on core 0, because core 1 is busy with D1–D3.

**The budget today (the 0.15 build, `nm build/elixir.elf`)**

| | Used | Total | Free | Usable (above build.py's guards) |
|---|---|---|---|---|
| Fast RAM (.data + .bss) | 66,004 B | 73,728 B | 7,724 B | ~3.6 KB |
| Pool | 347,268 B | 368,640 B | 21,372 B | ~13 KB |
| App flash | 439,504 B | 581,564 B | ~142 KB | ~140 KB |
| A kit | 377 B | the library's message: KIT_LEN + 16 ≤ LIB_MAX 400 | 7 B | then LIB_MAX must grow (`lib` is in the pool: ~3.3 B of pool a byte) |
| Project slot | busy 35.8 KB | 48.9 KB | ~13 KB | dense projects already fail |

The RAM / pool line only splits one SRAM, so the real spare is ~17 KB in all. Flash and CPU are not the limit for anything below; **SRAM is**, and only the delay line and the reverb need much of it.

**CPU.** 1% of the CPU line is 29 µs of the 2.9 ms half buffer, about 10,400 cycles at 360 MHz: **~80 cycles a sample**. Measured on the unit (Phase 17): a plain LXR voice ~6%, the delay 3%, Felucca's reverb 4–5% plus ~5% of send mixing under MIXER; stock kits ~27% on core 0; core 1's worst (two models, morphing) 62% average, 71% peak.

**The catch: the effects sit on the critical path.** Each block core 0 renders SN–HH, *waits* for core 1's D1–D3, then runs the mixer, the FX bus and the reverb (`voices_block`). With two heavy models the block is ~71% (core 1) + ~15% (mixer, FX, reverb) + the sequencer: roughly 10% to spare. Every percent added to the effects adds to that worst case directly, even though core 0 sits idle for ~50% of the block while it waits. With plain kits there is plenty of room (~70%).

**Estimates** (target cycles from the ~80 cycles/sample rule and the measured parts; ± half. "Pool" includes the three spare patterns, so a per-pattern byte costs 35 B)

| Feature | CPU (core 0) | RAM | Pool | Flash | Project / kit |
|---|---|---|---|---|---|
| 1A Global scale (quantizer) | ~0 (a table look-up per hit) | ~16 B | 64 B (root + scale per pattern) or 0 (per project) | ~1 KB | 2 B project or pattern; 7 B per pattern for the tracks' follow flags (245 B pool) |
| 1B Melody generator | ~0 (per hit, ~200 cycles) | ~300 B (7 tracks' loop memory and state) | ~1.5 KB (6 settings × 7 tracks × 35 patterns) | 4–6 KB | settings per track per pattern |
| 1B' Note length (gate) | ~0 | ~30 B | 0 (3 spare bits per step, see below) | ~1.5 KB | 1 B per voice (gate mode), or none |
| 1C Third global LFO | ~0.05% | ~40 B | 0 | <0.5 KB | 7 B project (format bump) |
| 1D Density / chance | ~0 | ~16 B | ~250 B (a per-track chance per pattern) | ~1.5 KB | 1–2 B project |
| 2A Tape / dub echo (in the FX bus's line) | 3–4% (today's delay 3%) | ~100 B | 0 (the same 64 KB line) | ~3 KB | ~4 B kit |
| 2A' Pitch-shifted repeats | +1–2% | ~40 B | +4 KB (a 2048-sample window) | ~1 KB | 1 B kit |
| 2A'' Echo into the reverb | +0.3% (plus the reverb, then running) | 0 | 0 | small | 1 B kit |
| 2B Dattorro plate (Clouds), half rate | 3–5% (HALL, also half rate, is 4–5%) | ~100 B | +10.6 KB (scaled) or +20.8 KB (Clouds' lengths) | ~2 KB | 1–2 B kit |
| 2C Master dirt stage (stereo) | 1–2% (2× oversampled: 3–4%) | ~60 B | 0 | ~1.5 KB | ~3 B kit |
| 2C' More per-voice shaper modes | ~0 (one curve replaces another) | 0 | 0 | ~1 KB | 0 (the shaper byte exists) |
| 18b Effects a block behind | ~0 (saves up to ~15% of the worst case) | ~1 KB (a block of sends, twice) | 0 | small | 0 |

Every row is cheap except two: the plate needs 10–21 KB of SRAM and the effects need the critical-path fix (18b) before they grow. The kit's 7 spare bytes run out after about two features: raising LIB_MAX by 16–32 B is part of 18.

#### Phase 18: groundwork (built in 0.16-test1, 2026-10-27)

**Built.**
- **18a.** The RAMTEXT gap is free: the AC79 SDK's own apps (`cpu/wl82/sdk_ld_sfc.c`) put ordinary RAM from 0x01C00000 (the TLB only with the MMU, which Elixir doesn't use), the boot ROM leaves nothing there, and the update loader runs only after a reset (its own map, 0x01C0A800). The 8 KB above it is *not* free: crt0 writes its status record at 0x01C07F00. A `.ram_buf` section (app.ld, `RAMBUF`, zeroed in `fm1_cstart`) now follows the RAM code there, and holds the reverb's buffers (ROOM's combs and allpasses, HALL's state: 13,156 B): the pool gets 13 KB back. build.py reports it (`RAMTEXT: code 3124 B, .ram_buf 13156 B, 8296 B free`). On the unit: any reverb that sounds as before proves the RAM.
- **The stacks:** `stacks_paint` (main.c) marks the user (24 KB) and supervisor (8 KB) stacks at power-on below where it runs; CONFIG → LOAD shows their deepest use (`STACKS u + s B`, red past three quarters) under the cores line. To read on the unit after a busy session.
- **18b. The effects a block behind, on core 0 while it waits** (`fx_early`, voices.c). Core 0 renders SN–HH, then runs the reverb on the *last* block's send (its return into `rev_wl`/`rev_wr`, added after the mixer: one block, 1.45 ms, late) and the delay's reads, then waits for core 1, then the mixer and the rest of the FX bus. The FX bus is an insert (a voice's dry part and its bus part meet in the mix), so it can't be a block late; only the delay's reads move: `delay_far` checks that every read of the block is a block or more behind the write head (delays from ~1.5 ms), and then `delay_reads` reads the echoes early, exactly as the loop would (no latency; shorter delays read between the writes as before). One core does the same in the same order, so the sound doesn't depend on the cores. JIANT's range check on the reverb's return: past 2^22, the lines are cleared and the block muted.
- **18c.** LIB_MAX 400 → 432.
- **Tests:** render_test renders every voice through the delay (a time change, a crossfade and a waiting time) with the reads early and with the effects all after the mixer (`FX_NOW`): the same bit for bit, 50 of 64 blocks read early; and each reverb model's return (the dry run subtracted) is the same one block later, bit for bit.
- **Cost:** RAM .data+.bss 66,004 B as 0.15; pool 334,212 B (−13 KB); image 440,672 B.

**The plan, as written before it was built:**

- **18a. Find SRAM.** The candidates, each to verify before it is used:
  - **The RAMTEXT gap.** `app.ld` gives RAMTEXT 0x01C00000–0x01C06000; `.ram_text` and `.c1_text` end at 0x01C00C34, so ~21 KB of it is unused, plus 8 KB at 0x01C06000–0x01C08000 kept "for RAM-run debug". Check that nothing uses it while the app runs: the boot ROM, the SPL, core 1's start, the update loader (it runs only after the app is gone). If it's free, a `.ram_buf` section there holds the plate's lines.
  - **The stacks.** 24 KB user + 8 KB system (0x01C74000–0x01C7C000). Paint them at boot and show the high-water mark on LOAD; a few KB may come back.
  - Last resorts, with costs: a union of COMB's line (8 KB) and REVERSE's recorder (11 KB), so the two can't play together; the screen canvas (30 KB) drawn in bands (a big change).
- **18b. The effects a block behind, on core 0, while it waits** (JIANT's idea, as 17d, but on core 0). While core 1 renders D1–D3, core 0 renders SN–HH and then runs the FX bus and the reverb on the *previous* block's sends; after the wait, only the mixer. The wet sound is one block (1.45 ms) late, which is inaudible. The delay reads one block shorter so synced echoes land exactly. The worst case drops by ~10–15%, and new effects fill core 0's idle time instead of adding to the block. It needs JIANT's output range check (mute, clear the lines, one core). render_test: the effects' output, one block late, bit for bit.
- **18c. Room in the kit.** LIB_MAX 400 → 432 (one static assert, ~100 B of pool), so the FX work below can add kit parameters. Kits in the library's flash entries (480 B) already have room.
- **One-core units.** Everything here also lands on a unit that fell back to one core; 17f's governor (shed load in a fixed order) becomes more important. Suggested order: the plate to HALL, the pitch shifter off, oversampling off.

#### Phase 19: global musical functions (built in 0.16-test1, 2026-10-27)

**Built.**
- **19a. Scales.** 17 scales (the list below has 17: CHROMATIC and 16), as 12-bit masks in seq.c (`SCALE_MASK`); the Marbles weights wait for Phase 20. Per pattern: ROOT, SCALE and FOLLOW (a bit per track, D1–D3 by default) on a new EUCLID page, SCALE (the pages: GENERATOR, ACTION, TRACK, SCALE, BOUNCE, LINKS; FOLLOW also on TRACK's KNOB 4). Absolute, as decided: `scale_fit` takes the voice's sounding semitone (its coarse tune + the note) to the nearest degree (a tie: the lower) and moves the note by the difference, so fine tune is kept. It quantizes whatever the sequencer plays through `seq_trigger` (the steps' notes, the links' pitch, ratchets, bounces), random pitch per hit (whole degrees, up to 7 either way), the ghosts, KEYS, and MIDI notes in (key_service). KEYS: the white keys play the degrees from the root at or below F3 (`key_scale_note`), the black keys stay chromatic. A blank pattern takes the project's last scale (`scl_last`, KSC1).
- **19b. Note length.** GATE per voice (the amp EG page, `PAR_GATE1..`): the attack's top held (`EG_H`) while the note lasts. A step's length in cond's top 2 bits and prob's top bit (`step_len`), on STEP TIMING's KNOB 4: AUTO, 1/32, 1/16, 1/8, 1/8., 1/4, 1/2, TIE. A set length sends MIDI's note-off at its end (AUTO keeps the LXR's: the next tick); TIE slides into the next note (`voice_legato`: the pitch, nothing struck; MIDI: the new note on, then the old off). Every reader masks the bits (`step_cond`, `step_prob`, the library's import).
- **19c. LFO 3**, PERF pages 9–11, and the new destinations DENSITY, TRANSPOSE (±7 degrees), the delay's FEEDBACK and TIME (±1/4 of the time, moved every 16 blocks), the REVERB send, MACRO 1–4 (every 8 ms while playing), and MELODY CHANGE / SPREAD (Phase 20). The new ones move either way from where they are (`gl.bi`).
- **19d. DENSITY**, PERF page 3: DENSITY, GHOST VEL, BUILD; the black keys leave tracks out (struck through on the page), as designed. Thinning by a fixed rank (place in the bar, velocity, a per-pattern order); at −100% only the bar's first steps are left. GRIDS and TURING tracks get their density pushed instead (`gen_dens`). Saved with the project (KSC1).
- **Project format 10:** the pattern's scale (only when not default), the step lengths (in cond's and prob's bits, already saved), KSC1 (6 B), LFO 3 in KGL1.
- **Tests:** ui_test phase19 (the scales, KEYS in scale, a cleared pattern's scale, the project round trip, GATE and the MIDI note-off at a 1/4's end, TIE, DENSITY's order, ghosts and ratchets, the DENSITY page, LFO 3 on TRANSPOSE and LFO 2 on DENSITY); render_test as Phase 18 (bit for bit).
- **Cost:** RAM .data+.bss 66,252 B (+248); pool 335,700 B (+1.5 KB); image 446,568 B (+5.9 KB).

**The plan, as written before it was built:**

- **19a. A global scale.** A root (C … B) and a scale. The list (16, each a 12-bit mask plus a weight per degree for 20's generator, in flash):
  - CHROMATIC (off: as today), MAJOR, MINOR, DORIAN, PHRYGIAN, MIXOLYDIAN, LYDIAN, HARMONIC MINOR, MINOR PENTATONIC, MAJOR PENTATONIC, BLUES;
  - chord tones, useful for dub techno and for bass: MINOR 7 (1 ♭3 5 ♭7), FIFTHS (1 5);
  - a few exotic: PHRYGIAN DOMINANT (Hijaz), HIRAJOSHI, PELOG (5-note approximation), WHOLE TONE.
  - **What it quantizes**, on the tracks that follow it: the steps' notes, KEYS, random pitch per hit (then whole scale degrees instead of a continuous bend), the links' PITCH, the bounces' drift, DRILL's and the ratchets' random notes, the melody generator. MIDI notes in, optionally.
  - **KEYS in scale:** the 16 white keys play the scale's degrees, two octaves and more, so nothing on the keyboard is out of key. A natural fit for the FM-1.
  - **How a voice stays in key.** A voice's pitch is its tune (coarse) plus the step's note − 60 (`osc_recalc`). Two ways, a choice for the user (below): *absolute*, quantizing the sounding semitone (tune + note), so every following voice is in key whatever its tuning, but a drum tuned off the scale moves by a semitone; or *relative*, quantizing only the note's offset, so kits sound as before but each voice must be tuned to the root by ear. Fine tune is kept either way.
  - Off by default (CHROMATIC), so no kit or project changes sound until a scale is chosen. Follow per track on the TRACK page, by default D1–D3 on, SN–HH off.
  - **Decided (the user, 2026-10-27): absolute**, with follow per track; **per pattern** (root + scale, 2 B a pattern). A blank pattern (empty, or cleared) starts with the scale last chosen: the project keeps that last root and scale (2 B), and selecting, clearing or first editing an empty pattern copies them in. Copying a pattern copies its scale.
- **19b. Note length.** Today a hit runs the amp EG's attack and decay and nothing ends it; MIDI's note-off waits for the next hit.
  - **A GATE mode per voice** (the amp EG page): attack, then hold for the note's length, then the decay. OFF is the LXR's (the default, so kits are unchanged). Plaits' LPG stays open while held, so P VA or P WTABLE can play a held bass note or pad.
  - **A length per step:** AUTO (the EG's), 1/32, 1/16, 1/8, dotted 1/8, 1/4, 1/2, TIE (to the next note: legato). Eight values, 3 bits, and there are spare bits in every step: `cond` uses 6 of its 8 (44 conditions), `prob` 7 (0–100), `note` 7. Using cond's top 2 bits and prob's top bit costs no pool. To check: every reader masks them (`cond_ok`, the `rnd > prob` tests, project.c's `c < NCOND` on load, LXR import), and project.c saves them (a step flag when set, so projects grow only where lengths are used).
  - MIDI out sends the note-off at the length's end, so Elixir can sequence a synth over USB with real lengths.
- **19c. A third global LFO.** `NGLFO` 2 → 3, two more PERF pages (decided: 15 in all, plus 19d's DENSITY page: 16), 7 B more of project (format version 10). New destinations, for all three: DENSITY (19d), TRANSPOSE (scale degrees, 19a), the melody's CHANGE and SPREAD (20), the delay's feedback and time warp, the reverb send, and **a macro** (MACRO 1–4: through the macros the LFO reaches any lockable parameter; applied every 8 blocks to keep it cheap).
- **19d. Density across all tracks** (designed with the user, 2026-10-27). One control that makes a pattern more minimal one way and builds it up the other, from a knob or a global LFO.
  - **A DENSITY page on PERF** (after MORE). KNOB 1 **DENSITY**, −64 … +63 shown as −100 … +100% (0: as programmed). KNOB 2 **GHOST**: the velocity of added notes (default 40). KNOB 3 **BUILD**: what the positive side adds: CHANCE (only raises the steps' chances to 100%), GHOSTS (then fills empty steps), RATCHETS (then also adds ratchets of 2 to the strongest hits). Saved with the project, so it holds across pattern changes, as a performance control should.
  - **Excluding tracks, any number, no extra screens:** on the DENSITY page the black track keys 1–7 don't select tracks, they toggle each track's exclusion. The page draws a row of the seven track names, excluded ones crossed out (like the mute row). Default: none excluded; press D1 to keep the kick as programmed. 7 bits in the project. (SHIFT + a track key still mutes, as everywhere.)
  - **Thinning, musically (negative side):** steps drop in a fixed order, weakest first: off-beat 16ths before 8ths before beats, quiet before loud, among equals a per-pattern random order (as Grids' density). Turning it down then thins the groove steadily and doesn't flicker at random; the first step of each bar goes last. Locks, conditions and ratchets of the steps kept are untouched.
  - **Building up (positive side):** the chances of the existing steps rise towards 100% first, then (BUILD) ghost notes fill empty steps at the GHOST velocity in the reverse order (the strongest empty positions first: 8ths, then 16ths), at the track's last played note (or the melody's, Phase 20, if it runs on that track).
  - **A global LFO destination DENSITY** (19c), from the knob's position either way by the depth: a 64-bar triangle breathes the whole pattern from sparse to busy.
  - The generators follow it: GRIDS' and TURING's own densities move with it on tracks not excluded.
  - Not done (simpler): a per-track CHANCE moved by Control All. The global knob and the exclusions cover the use; it can come later if wanted (~250 B of pool).
  - Cost: CPU ~0 (a compare per step), RAM ~16 B, project 3 B, flash ~2 KB.

#### Phase 20: a melody generator (built in 0.16-test1, 2026-10-27)

**Built.**
- **Where:** two pages after ACTION on the generator's set (ARP twice): MELODY (SHAPE, SPREAD, BIAS, STEPS) and MELODY: LOOP (DÉJÀ VU, LOOP 1–16, LENGTH, LEN VAR). 8 B per track per pattern (`pattern_t mel`, ML_*), saved in the version 10 track block. Beside any rhythm generator or none: it doesn't play from the live copy (`gpat`), it picks the note as each hit plays (`step_hit` → `mel_note`), so grid edits are heard at once.
- **The notes:** degrees of the pattern's scale (19a), thinned by STEPS: every degree, then the chord tones (root, third, fifth, seventh), the triad, root and fifth at the top (weights by interval: root 4, fifth 3, third 2, seventh 1). The root sits at the voice's tune (as 19a's absolute scale), BIAS moves the centre ±1 octave, SPREAD the range (to ±1 octave either side, wider with ARP). RANDOM: a bell round BIAS. WALK: one or two degrees up or down from the last, back the other way at the range's edge. ARP UP / DN / U/D: the chord tones. MOTIF: four notes, then up two degrees, inverted round the first, backwards ending on the root (16 notes); a new motif with DÉJÀ VU's chance.
- **DÉJÀ VU:** the chance of a new note in the loop's place is ((127 − DV) / 127)³: 0 always new, the middle "now and then" (~12%), 127 locked. The loop keeps semitones and is read through STEPS as it is now, so turning STEPS up simplifies a looping phrase.
- **LENGTH / LEN VAR:** 19b's lengths; LEN VAR the chance of another (1/16, 1/8, 1/8., 1/4, TIE, 1/32, weighted to the first).
- **COMMIT / UNDO / RESET** on ACTION whenever a melody runs: COMMIT writes the notes and lengths as they last played (`mel_n`, `mel_l`, kept after STOP) into the steps; RESET starts the melody (and its loop) again. The melody stays on after COMMIT (turn SHAPE OFF to keep the steps' notes).
- **The global LFOs:** MELODY CHANGE (DÉJÀ VU down, so more new notes) and MELODY SPREAD, either way.
- **Credit:** after the design of Marbles' X section (MIT, `LICENSES/MIT-Marbles.txt`); no Marbles code copied. The weighted scale tables and Marbles' beta distributions are left out (simpler).
- **Tests:** ui_test phase20: the pages and defaults, RANDOM in the scale and changing, a locked loop of 4, STEPS at the top (root and fifth only), WALK's steps, ARP UP, MOTIF's fourth phrase, a length held, COMMIT and UNDO, the project, LFO on CHANGE.
- **Cost:** RAM .data+.bss 66,572 B (+320); pool 338,556 B (+2.9 KB: the patterns' 8 B a track, `mel_n`, `mel_l`); image 449,736 B (+3.2 KB). CPU: a few hundred cycles a hit.

**The plan, as written before it was built:**

The rhythm generators decide *when* a track plays; this decides *which note*. It goes on its own page after GENERATOR (MELODY), separate from the generator slot, so EUCLID, GRIDS or TURING can make the rhythm while MELODY makes the notes, on the same track.

- **The engine: Mutable Instruments Marbles' X section** (MIT, Emilie Gillet, as Rings and Plaits): random values with SPREAD (range), BIAS (register: low … high) and STEPS (from smooth to quantized to the scale, and then to fewer, stronger degrees: at the top only the root and fifth: Marbles' weighted scales), and **DÉJÀ VU**: 0 new notes each time, the middle a loop of LENGTH notes (1–16) that changes now and then, the top a locked loop. Déjà vu is what makes it evolve and still sound like a phrase. A global LFO on it lets a melody wander away and come home over 64 bars.
- **Shapes** (KNOB 1 on the page): RANDOM (Marbles), WALK (steps of one or two degrees, weighted: smooth lines, good for bass), ARP (the scale's chord tones up, down, up-down), MOTIF (a 4-note motif from the random source, transposed, inverted or reversed each bar, ending on the root every 4 bars: call and response).
- **Note lengths:** LENGTH (the usual length) and LEN VAR (how often a note gets a shorter or longer length or a tie, weighted towards musical values). Uses 19b's gate, so it needs 19b first, and 19a's scale.
- **Live, like TURING:** the pattern doesn't change; COMMIT writes the notes and lengths into the steps (UNDO takes it back). Saved per track per pattern (6 settings).
- **Where it sounds good:** D1–D3 with tuned models (Plaits VA, WTABLE, FM, CHORDS, STRING; Rings), SN's tonal oscillator. The hats, less so.
- Cost: nothing on the CPU; ~300 B RAM, ~1.5 KB pool, 4–6 KB flash. Credit Marbles (MIT) in NOTICE.md and LICENSES/.

#### Phase 21: the delay (dub echo, sync, glitch) (built in 0.16-test1, 2026-10-27)

**Built.**
- **21a. TAPE, TAPE PP:** DELAY's MODE 2 and 3 (`fx_tape`, fxbus.c). The line at half rate in the same `fxbuf` (1.49 s mono, 0.74 s a side), the block's input pairs averaged in, the echoes read between samples and interpolated back up. P1 TIME (10 ms to the most, squared), P2 WOW (wow: a random drift every ~0.19 s up to ±1.8 ms; flutter ~6 Hz up to ±0.14 ms), P3 TONE, P4 FEEDBACK to 115%. In the loop: low cut ~100 Hz, TONE, PITCH, then a cubic soft clip whose ceiling SATURATE lowers from full scale to a quarter (feedback past 100% grows into a bounded roar: tested, the last second of 4 s at 115% peaks at 4,500). A time change glides the read head (~90 ms: the pitch bends). NORMAL and PINGPONG untouched (render_test bit for bit); the 18b early reads stay theirs. Switching to or from a TAPE type clears the line (another rate).
- **21b. SYNC** (all four modes): TIME's 12 steps are the note values; too long for the line, halved (the card shows `1/2 /2`, `/4`). A tempo change re-sets a synced NORMAL/PINGPONG time (crossfaded, `fx_tempo`); TAPE glides there. All in 32-bit arithmetic (`sync_len`).
- **21c. ECHO > REVERB** (all modes): the echoes into the reverb's send (`rev_in`; the reverb runs a block behind anyway). **THROW ECHO:** both PERF banks' keys are taken, so THROW REV, held while the bus is on DELAY, also throws the echoes into the reverb (dub's move).
- **21d. GLITCH** (TAPE modes): at each repeat (the delay's length), GLITCH's chance of STUTTER (its first 1/32 or 1/64 at the tempo, looped), REVERSE, SKIP (1–3 slices earlier) or HALF (half speed), faded 32 samples in and out; skipped when the line can't hold it.
- **21e. PITCH** (TAPE modes): OFF, +12, +7, −12, DETUNE (+12 cents), two crossfaded heads over a 2,048-sample window (1,024 a side in PP; `ps_buf`, 4 KB pool) in the feedback: each repeat shifted again. Tested: 441 Hz back at ~870 Hz on the second repeat.
- **Kit:** 5 bytes (`PAR_DSYNC`, `PAR_DSAT`, `PAR_ECHOREV`, `PAR_GLITCH`, `PAR_DPITCH`); SATURATE, ECHO > REVERB and GLITCH morph, SYNC and PITCH don't. Pages: FX's 4th (DELAY: DUB: SYNC, SATURATE, GLITCH, PITCH) and 5th (DELAY: SEND: ECHO > REV). TAPE's P2 is named WOW.
- **Tests:** ui_test phase21 (SYNC 1/8 on NORMAL and TAPE: 11,025 samples; the second echo; TAPE PP's sides; the roar bounded; GLITCH and PITCH bounded; PITCH +12's frequency; ECHO > REVERB; the pages). The cost: TAPE PP with everything on, 0.65 µs a block on the host (~65 µs on the unit by the rule of thumb, ~4% of a block): **to measure on the unit** (LOAD).
- **Cost:** RAM 66,688 B (+116); pool 342,984 B (+4.4 KB); image 452,528 B (+2.8 KB).
- **Not done:** the XoX and FM1 Open delays weren't read before writing it; the design follows the plan.

**The plan, as written before it was built:**

The FX bus has one 64 KB line (`fxbuf`, 0.74 s mono, 2 × 0.37 s ping-pong at 44.1 kHz) and runs one effect at a time. There's no SRAM for a second line, so the new delay is new DELAY **types** in the same line, beside the LXR's NORMAL and PINGPONG, which stay exactly as they are (LXR kits sound the same).

- **21a. TAPE (and TAPE PP, ping-pong).** The line is stored at half rate (22.05 kHz, as HALL already runs): **1.49 s mono, 0.74 s a side**, in the same memory. A tape echo is dark anyway (the tail stops near 10 kHz). In the feedback: a low cut (~100 Hz: the repeats thin out instead of booming), TONE (low-pass), saturation (soft, rising with feedback, so feedback past 100% grows into a controlled roar instead of clipping: dub), and WOW and FLUTTER (two slow random LFOs on the read position). Changing the time glides the read head, as a tape's motor does (the pitch bends), instead of today's crossfade.
- **21b. Time as note values.** With SYNC on, TIME steps through 1/32, 1/16T, 1/16, 1/8T, dotted 1/16, 1/8, 1/4T, dotted 1/8, 1/4, 1/2T, dotted 1/4, 1/2, at the tempo (MIDI clock too). What fits: TAPE 1.49 s holds a dotted 1/4 down to 61 BPM and a 1/2 down to 81; NORMAL 0.74 s holds a 1/4 down to 81 BPM but a dotted 1/4 only above 122. A value too long shows as such and plays at half. A tempo change glides.
- **21c. Into the reverb.** ECHO → REVERB (0–127): the echo's output fed to the reverb's send, the classic dub chain (echo, then spring or plate). A PERF throw on it too (THROW ECHO).
- **21d. Glitch, from the same line (IDM).** A GLITCH amount: the chance each repeat is changed. The changes: STUTTER (the repeat loops its first 1/32 or 1/64), REVERSE (the repeat played backwards: a window read the other way), SKIP (a random earlier slice instead of the last one), HALF (read at half speed, an octave down). STUTTER, REVERSE, SKIP and HALF only read the line in other ways: no memory, little CPU.
- **21e. Pitch-shifted repeats** (optional, +4 KB, +1–2%): Clouds' pitch shifter (two crossfaded read heads over a 2048-sample window) in the feedback: each repeat +12, +7, −12 or a detune. Shimmer when it feeds the plate.
- **Decided (the user, 2026-10-27):** all of 21a–21e as proposed.
- **The one-slot problem:** with the FX bus on DELAY, it can't also drive. 23b (a drive before any FX type) solves that for dirty echoes.
- Kit: TAPE reuses P1–P4 (time, wow, tone, feedback); new bytes for SYNC, saturation, ECHO → REVERB and GLITCH (~4 B, so after 18c).
- Reference: XoX (Charles Vestal, GPL-3.0, already credited) has a tape delay send on this same hardware; read it before writing 21a. FM1 Open (M-VAVE's official open firmware, 2026-10) has delay and reverb sends; check its licence before borrowing anything.

#### Phase 22: a plate reverb (Clouds' Dattorro) (built in 0.16-test1, 2026-10-27)

**Built.**
- **PLATE**, the reverb's fourth MODEL (`rev_plate`, reverb.c): Clouds' reverb as it is (the 4 diffusers, the first smeared by its 0.5 Hz LFO inside its loop; the figure eight of two branches, the first read through the 0.3 Hz LFO), at half rate (22.05 kHz, as HALL) with Clouds' lengths × 0.689 (11,283 samples). The lines: the diffusers in ROOM's allpass buffer, two in its combs, four in `pl_buf` (11.9 KB pool). SIZE: Clouds' reverb time 0.35–0.98 (SPACE 0.995: it holds); DAMP: the low-pass; diffusion fixed at Clouds' 0.625 (no new knob); LEVEL as the others. Mid and side out (left from one branch, right from the other).
- **Fixed point:** int16 lines at 16,384 to the unit (finer than Clouds' own 12-bit format; 2× headroom on a send in at 0.2). The products round to the nearest while loud (towards zero, the tail died far sooner than Clouds' float) and towards zero once quiet (`plt.lo`: it ends in exact silence and sleeps, no limit cycle). The low-passes are written towards their input (no stuck DC).
- **Level:** PL_OUT 6, near HALL's (a 490 Hz burst a little under, a kick's tail ~3 dB over).
- **Tests:** `tests/reverb_compare.sh` (Clouds' reverb.h in a copy, lengths scaled, LFO swings off, float lines; run with `EURORACK=… STMLIB=…`): correlation 0.98–0.998 over the first second at three settings, the energy at 0.5/1/2 s within 0.5–2× of Clouds' where it is above the lines' floor. ui_test: the reverb loop now covers PLATE; phase22 (it rings and dies away, sleeps by 10 s at SIZE 64, a stereo return, SPACE holds, the page). The cost: 0.5 µs a block on the host (SPRING's; HALL 0.25): ~3.5% of a block on the unit by the rule of thumb, on core 0 while the second core renders.
- **Cost:** RAM 66,784 B (+96); pool 354,892 B (+11.9 KB, 13.7 KB left); image 454,896 B (+2.4 KB).

**The plan, as written before it was built:**

- **The design:** Clouds' reverb (MIT; Rings and Elements use the same one), Jon Dattorro's plate: four input allpasses, then a figure eight of two branches, each two allpasses, a delay and a one-pole low-pass, read through two slow LFOs. Ten lines, 16,375 samples at Clouds' 32 kHz (`FxEngine<16384, FORMAT_12_BIT>`). Its knobs map onto the reverb page's: SIZE → the decay ("reverb time"), DAMP → the low-pass, plus DIFFUSION; LEVEL as now.
- **Add it, don't replace:** PLATE as a fourth model after ROOM, SPRING and HALL. One model runs at a time and they share one buffer, so adding costs the same memory as replacing, and kits made with Felucca's models keep their sound. (Replacing Felucca's reverb would also change every saved kit's sound.)
- **Rate and memory** (the decision for 22):
  - at half rate (22.05 kHz, as HALL) with Clouds' lengths scaled to it: the same plate as Clouds, 11.3 K samples, 22.6 KB: **+10.6 KB** over today's reverb buffers (~12 KB). Fits in today's pool, with ~2.5 KB left.
  - at half rate with Clouds' lengths as they are: a tank 45% larger (longer, darker, bigger modes: very dub), 32.8 KB: **+20.8 KB**. Needs 18a's RAMTEXT gap.
  - Full rate (44.1 kHz) would need 45 KB: not possible.
  - **Decided (the user, 2026-10-27): Clouds' own plate, small** (the first: lengths scaled to 22.05 kHz, +10.6 KB). As resource-efficient as possible; the bigger tank is not built. Added as the fourth model beside ROOM, SPRING and HALL (the recommendation; the same memory as replacing, and saved kits keep their sound). Credit Clouds (MIT) in NOTICE.md and LICENSES/.
- CPU: 3–5% (as HALL); with 18b it runs in core 0's wait.
- `tests/`: a reverb_compare against Clouds' float code, as for Rings and Plaits.
- SPACE (PERF) holds its tail as it does the others.

#### Phase 23: dirt (built in 0.16-test1, 2026-10-27)

**Built.**
- **23a. Four more SHAPEs** per voice (`shape_block`, voices.c; NSHAPE 9): FUZZ (a gain of 4–56 into a hard clip, the quietest of the tail gated down), TAPE (a cubic soft clip, `soft3`, then a high cut from ~11 kHz to ~5 kHz with DRIVE), SINE FOLD (a sine of the input: West Coast folds, two turns at the most), BITE (the highs pushed, then a biased soft clip harder with DRIVE, its DC taken out). One more state per voice (`sh_z`). LXR kits are untouched (their shapes are the first five).
- **23b. BUS DIRT** (`dirt_bus`, fxbus.c): the FX bus's input driven before its type, any type (OFF too: the bus as a drive), a gain of 1–8 into `soft3`, the level trimmed back as it rises. Dirty echoes and phasers at last (21's one-slot problem).
- **23c. The master's DIRT** (`dirt_master`, on the mix before it is saturated to 16 bits and before the DJ filter, engine.c): TYPE (TAPE, TUBE, FUZZ, DIODE, FOLD) and AMOUNT (0: off, nothing touched). Each type's gain, high cut, dry/wet (FUZZ and FOLD parallel: the kick's punch kept) and level trim follow the amount; the wet part fades in over the first quarter (no step). Not oversampled (FUZZ and FOLD alias a little; the 2× option is left for later).
- **Kit:** 3 bytes (`PAR_FXDIRT`, `PAR_MDIRT`, `PAR_MDIRT_AMT`); the amounts morph and lock, the TYPE doesn't morph. The page: FX's 6th, DIRT (BUS DIRT, MASTER, AMOUNT).
- **Tests:** ui_test phase23 (each new shape bounded and adding harmonics; each master type bounded and within ±6 dB of the dry level; AMOUNT 0 and BUS DIRT 0 bit for bit; BUS DIRT drives; the parameters set the stages; the page). render_test unchanged (off: bit for bit). The cost on the host: the master 0.22–0.36 µs a block (~2.5% on the unit by the rule of thumb), a shape 0.05–0.10 µs a voice.
- **Cost:** RAM 66,820 B (+36); pool 355,088 B (+196); image 456,464 B (+1.6 KB).

**The plan, as written before it was built:**

What's already there: per voice, a shaper (LXR, FOLD, CLIP, ASYM, RECTIFY), drive, bits; the FX bus's DRIVE (tube, fold, rat); PERF's CRUSH on the master. None of it is touched.

- **23a. More per-voice shaper modes** (free: the shaper byte exists, one curve replaces another): FUZZ (high gain into a hard clip, a little gating), TAPE (soft, with a high cut), SINE FOLD (a sine wavefolder: West Coast), BITE (Elektron-style overdrive). On D1–D3 this runs on core 1, the rest on core 0.
- **23b. A drive before the FX bus** (a DIRT setting on the FX bus, any type): drive into the tape echo, into the phaser. ~1%. This also solves 21's one-slot problem.
- **23c. A master DIRT stage** (global, part of the kit, as Digitakt's master overdrive): TAPE, TUBE, FUZZ, DIODE, FOLD, with DRIVE, TONE and MIX (parallel: keeps the kick's punch), before the DJ filter. 1–2% stereo; FUZZ and FOLD alias, so a 2× oversampled option (+1.5%). Off by default.
- **Decided (the user, 2026-10-27): all three.** 23a (new per-voice shaper modes), 23b (a drive before the FX bus) and 23c, the master stage kept simple: **TYPE and AMOUNT** (off at 0; the tone and the dry/wet mix set by each type, so two knobs and 2 kit bytes). A TONE knob can come later if a type needs it.

**Order (recommended):** 18 (a, b, c) → 19 (a, b, then c, d) → 20 → 21 (a, b, c, then d, e) → 22 → 23. 19 and 20 are cheap and independent of the FX; 21 and 22 need 18. 21 before 22 because the delay costs no memory and 21c makes the existing reverbs dub-capable at once.

**Decided (the user, 2026-10-27)**
1. Scale: absolute, follow per track (D1–D3 on by default).
2. Scale per pattern; a blank pattern starts with the scale last chosen.
3. MELODY on its own page, beside any rhythm generator.
4. Density: a DENSITY page on PERF (minimal one way, built up the other: chances, ghosts, ratchets), a global LFO destination, any tracks excluded with the track keys on that page (none by default). See 19d.
5. The plate: Clouds' original, small (+10.6 KB), as a fourth model. The delay: all of 21 as proposed.
6. The third LFO: two more PERF pages.
7. Dirt: new per-voice shapes, a drive before the FX bus, and a master stage with TYPE and AMOUNT.

**Still open (small, for when each phase starts):** whether the RAMTEXT gap (18a) is really free; BUILD's RATCHETS option on 19d (keep or drop after hearing it); the melody's shapes beyond RANDOM and WALK (ARP, MOTIF) if 20 runs long; a TONE knob on the master dirt.

**Next session:** 0.16 RELEASED 2026-10-28 (tag v0.16): Phases 18–23, the master compressor, STUTTER, SYNC's detent, and the unlisted mobile view (`mobile/`). To do: the user's unit test of 0.16 (the LOAD with TAPE PP, PLATE, the master DIRT and the compressor at once; STUTTER's mono repeats in a stereo mix); the mobile view on a real phone, then publicise it; the remote map for the compressor and STUTTER. Later: the LCXL templates (12f), BUILD's RATCHETS (keep or drop), a TONE knob on the master DIRT, a 2× oversampled FUZZ/FOLD, the second-screen mirror (below).

#### 0.16-test3 (built 2026-10-28): a master compressor, STUTTER, SYNC's detent

Asked 2026-10-28 (the user's last FX round before 0.16).
- **SYNC with TAPE "only one time".** Not reproduced: on the host and in the emulator every delay mode steps through all 12 note values (the card and the echo's time both checked at 120 BPM: TAPE's 1/16T 3,675 samples … 1/2 22,050 at half rate). What was there: a value took ~5 detents (0..127 over 12 values, 2 a detent), and TAPE glides to a new time over ~90 ms. Changed: with SYNC on, TIME moves a note value a detent (`sync_detent`, ui.c; locks too). To ask the user: the tempo, the mode, and whether a global LFO, a lock or morph was on TIME.
- **The master COMPRESSOR** (`comp_master`, fxbus.c; the mix after the FX bus and reverb, before the master's DIRT). Kit bytes `PAR_MC_RATIO` … `PAR_MC_MIX` (7, KIT_LEN 398): RATIO (OFF, 1.5:1 … 10:1, LIMIT; OFF by default: nothing touched), THRESHOLD 0 … −48 dB, ATTACK 0.1 … 100 ms, RELEASE 20 ms … 2 s, SIDECHAIN (MIX or D1 … HH), MAKEUP 0 … +24 dB, MIX. A peak follower each sample (Q27, Q24 coefficients), the gain every 8 samples (log2, a 6 dB soft knee) ramped. **Sidechained to a voice**, that voice's dry part is mixed into `mc_kl`/`mc_kr` (voices.c `mix_voice`'s new outputs) and added back after the gain: the kick isn't ducked by itself (decided without the user: what ducking is for). FX pages 7 and 8 (MASTER COMP, COMP: SIDECHAIN) with a gain-reduction meter. RATIO and SIDECHAIN don't morph.
- **STUTTER** (`stutter`, perform.c), PERF's page 4 (PERFORM: STUTTER): CHANCE, SIZE (1/8 … 1/64, triplets, RANDOM), LENGTH (1/16 … 1 bar), VARY (OFF, SHRINK, PITCH DN, PITCH UP, REVERSE, RANDOM). Every PERF key is taken (two banks of 9), so it has no key: OCT+ held on its page stutters at the next SIZE; a new global LFO destination STUTTER (GD_STUT, appended after the macros, unipolar) raises the chance. A burst: the first SIZE plays live while REVERSE's recorder (5,512 samples, mono, 22 kHz: no new memory) takes it, then the recorder holds and the slice repeats; the SIZEs are counted in samples from the burst's start (Q16), so it stays on the grid. The decision is at each LENGTH (the sequencer's ticks; stopped, its own clock), and again at a burst's end. Saved in KSC1 (6 → 10 bytes). Mono and dark (the recorder's), and a 1/8 below 120 BPM plays at half: noted in the manual.
- **Tests:** ui_test phase_016t3 (the detent both ways; COMP OFF bit for bit, 4:1 at −24 dB on a −6 dB sine: −13.6 dB (−13.5), D1 keyed ducks the rest and not its own part, back when D1 stops; STUTTER: the second 1/16 against the first 0.997, each VARY bounded, CHANCE 0 stops, the LFO destination, the page, OCT+ held, the project). render_test, library_test (KIT_LEN 398), install_test pass.
- **Cost:** RAM .data+.bss 67,612 B (+792: the key buffers and state); pool 355,552 B (+464); image 463,288 B (+6.8 KB). CPU: the compressor ~25 cycles a sample when on (~0.3%); STUTTER only while repeating, less.
- **To check on the unit:** the LOAD with the compressor on; how STUTTER's mono repeats sit in a stereo mix.

#### A bigger screen (research, 2026-10-28)

**Built: the mobile view** (`web/emu/mobile.html` → the site's `mobile/`, 0.16). It runs the same wasm and worklet as the full emulator; no code from hb3p8/fm1-mobile-emu (GPL-3.0, the same design: AudioWorklet, wasm, WebGL screen, PWA). **Unlisted:** no link to it anywhere on the site and `noindex`, while the user tries it on a phone; publicise it (nav link, home page) when the user is happy with it. Revised 2026-10-28 at the user's request, to look like the FM-1:
- round knobs as fm1-mobile-emu's (drag up or down, 10 px a detent): SELECT, ALGO, PRESET and VOL (absolute, an arc) on the left, KNOB 1–4 on the right, in one row;
- the buttons in the FM-1's two rows of seven, with their own names only;
- the 11 black keys in one row as on the unit (D1 … OH, then bars 1–4): no separate bar row;
- the white keys as 2 × 8 square pads;
- one screen high (`100dvh`, which leaves out Safari's bars), the screen taking what the controls leave, in whole firmware pixels when that costs ≤ 10%. No HOLD or MENU keys.
- a Kits sheet imports LXR-02 .SND files or a .zip of them into the library or the slots, and backs up or restores a .ktlb. Kits are kept in the browser, in the same storage as the full emulator. The help and the links are in the sheet.

Checked in Chrome at 393 × 659 (iPhone 15 Safari's room), 375 × 548 (iPhone SE) and 852 × 330 (landscape): nothing scrolls; on the SE the screen is 246 px and the pads 40 px. The zip reader matches all 64 HRTL kits byte for byte, deflated and stored. Not yet tried on a real phone.

**Not built: a second screen for the unit** (asked about Paleotone's method):
- **What the hardware allows.** The FM-1 can't drive a screen itself: its MCU has no DisplayPort or HDMI, and the 5–7″ USB-C monitors need one or a DisplayLink driver on a host. So a plug-in screen needs a host: an Android phone or tablet, a computer, or a Raspberry Pi with a 5″/7″ panel running Chromium. The FM-1 connects to it over USB MIDI, which is how Paleotone does it (MIDI out of the firmware, a companion app drawing). iPhone Safari has no Web MIDI, so on iOS this needs a native app (CoreMIDI sees the FM-1 class-compliant over USB-C) or nothing.
- **Mirror the pixels (the cheapest way).** Every draw goes through a canvas strip and `cv_blit`/`cv_blit_from`, which already call `GFX_HOOK_BLIT` (gfx.c; the host tests hook in there, a no-op in the firmware). So only changed strips are drawn: the dirty rectangles come for free. A hook would send each strip as SysEx: x, y, w, h and the pixels, run-length coded (the UI is flat colours, so RLE on RGB565 should shrink it about 5–20×), 7-bit packed.
  - Cost: a full 240 × 240 frame is 115 KB raw, ~132 KB as SysEx, and maybe 10–25 KB coded. A typical frame (a card's value, the playhead) is a few hundred bytes. USB full-speed MIDI moves well over that.
  - Firmware: a TX queue (~2–4 KB RAM, which we have) and ~1.5 KB of code. Lowest priority behind clock and notes: drop a strip and send a full refresh later rather than delay MIDI. Off by default, a GLO setting (SCREEN OUT).
  - The companion: a page on the site (`screen/`, Web MIDI: desktop Chrome/Edge, Android Chrome) that paints the strips scaled, with no smoothing.
  - It could be built and tested in the emulator first: the wasm's hook can post the same SysEx to a second window.
- **Send the state, draw on the far side** (Paleotone-like, richer). The companion would draw its own bigger UI (all 64 steps, the whole kit) from a state stream, using parameter NRPNs plus the step data. More to send and a lot more firmware, as the Paleotone author found, and two UIs to keep in step. Not worth it while the mirror would do.
- **Or no unit at all.** The mobile view on a Pi or tablet with a 7″ touch panel is Elixir on a bigger screen today. The FM-1 could then be its controller over the existing remote map (TRACKS ch 15, GLOBAL ch 16), but the sound would come from the host.
- **Recommendation if wanted:** a phase for the mirror (emulator first, then the unit: measure the USB load with clock and notes running), then the `screen/` page.

### Other ideas

- **More projects on the unit** (asked 2026-10-26). Today there are 4 projects and the session, each in a fixed 48 KiB slot, 6 slots in all (288 KiB at 0x97000; the spare slot makes a save safe if the power is cut), whatever their size. Measured with the firmware's own encoder:

  | Project | Saved size |
  |---|---|
  | Empty | 8.5 KB |
  | 8 patterns × 16 steps, a few accents, notes and locks, 8 kits | 11 KB |
  | 16 × 16, half the hits accented, 2 melodic tracks, 20% locked, 16 kits | 14 KB |
  | 32 × 16, the same, 32 kits | 20 KB |
  | 32 × 32, the same | 23 KB |
  | ui_test's "busy" one: 32 × 32, every step with its own velocity and note, every kit edited | 35.8 KB |
  | 32 × 64, every step with velocity and note, half locked | 46 KB |
  | ui_test's "dense" one: every field of every step | PROJECT TOO BIG |

  Packing projects by size (a log over the same 288 KiB, one free slot's worth kept for safe saves) would hold about 10–20 typical projects instead of 4. That means a project format and storage change, and a phase of its own. The library page's backups already keep any number on a computer.

- **303-style acid track:** about 33 KB of pattern memory at 64 patterns (less at 32), 1–2 voices' worth of CPU, and real UI work for track selection and note entry. Check charlesvestal's fm1-x0x (FM-1 303 firmware) and its licence first.
- **More FX buses:** cheap for drive, ring mod or compressor; costly if each has its own delay.
- **LXR-02 preset import:** Elixir uses the LXR's own parameter numbering. Either a computer script that sends a kit as CC/NRPN, or a converter to a Elixir project plus a USB transfer path. Check the LXR SD file format (lxr-desktop-app may read it).
- MIDI CC out when the panel's knobs turn.
