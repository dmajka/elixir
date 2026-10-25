# Kettle roadmap

## Where things stand (October 2026)

Kettle is the Erica Synths LXR-02 firmware (GPL-3.0) ported to the M-VAVE FM-1 on Felucca's hardware layer. See README.md for controls, building and installing.

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
  - A private-build kit bank (`KETTLE_KITS_DIR`).
  - The code is at https://github.com/dmajka/kettle.
- **0.8 (Phase 9, the LXR's live feel):** PERF's pages reordered (the performance pages, MIX, then the macros, then PERFORM 2), the LXR's PERFORMANCE page with its global sample rate (SRate), MIX pages for the voices' volumes; KEYS (SHIFT + EDIT) to play a voice, with a key velocity and accent; LXR project import (kits, patterns, songs); a kit library in flash (128 kits, 128 sounds) with SOUND save and load and morph from it; a per-track sound browser. See *Phase 9*.
- **0.9 (Phase 10):** the physical models replaced by a fixed-point port of Mutable Instruments Rings' modal resonator, string and FM voice, checked against Rings' own code. CPU on hardware still to be measured. See *Phase 10*.
- **MIDI:** USB and TRS in, USB out. CC/NRPN control as on the LXR; clock out always on; clock in by turning the tempo below its minimum to "MIDI".
- **Projects:** 4 slots plus an AUTO session slot in flash, 48 KB each, sparse encoding. Format version 7 since 0.9; versions 1 to 6 still load (6's models mapped to Rings').
- **UI:** mapped to the FM-1's keys, buttons and knobs; SHIFT legend on screen; mixer under GLO.
- **Firmware build:** `./build.sh` makes `build/kettle.fwsc` and passes Felucca's checks.
- **Browser emulator:** `web/emu/build.sh`, tested in Chrome, with Web MIDI in and out, including clock.
- **First hardware run (0.5.1):** installed on a real FM-1 from V15 with the install page (`web/make_site.py`); a battery display on CONFIG; autosave no longer makes a burst of noise after a stop. See *0.5.1* below.
- **Tests:** render_test, compare_test, library_test (the library page's code against the device's), rings_compare (the models against Rings' float code) and ui_test (75 screens; Rings' models; PERF's pages, MIX and KEYS; the kit library and sound browser; PERFORM 2; links between tracks; bouncing ratchets and the roll sweep; the output's hush and autosave's timing; the battery display; Grids' map; clocks, conditions, ratchets, generators, bar copy; locks, patterns, project save/load, a 0.1 project, sends, reverb, sound shaping, phaser, flanger, PERF's effects and macros, autosave failure, sync in) all pass.

**Measured resources** (from the 0.9 build)

| | Used | Total | Free |
|---|---|---|---|
| Fast RAM | 41 KB | 72 KB | ~31 KB (Rings' string lines: 18 KB of it) |
| Pool | 337 KB | 360 KB | ~23 KB |
| App flash | 312 KB | 581 KB | ~269 KB |
| Kit library (flash) | 72 KB at 0xEA000–0xFBFFF | | 128 kits + 128 sounds |

**CPU on a real FM-1 (0.5, first measurement):** about 60% average and 64% peak with a busy pattern. The 122 LATE buffers seen then were not the engine's load (a 64% peak leaves a third of each buffer spare) but autosave's flash erases, which hold the processor; 0.5.1 no longer counts those. The load is on screen (bottom of the CONFIG pages: average, peak since CONFIG opened, late buffers since power-on). The browser emulator always shows 0%, because it doesn't model CPU time. The second core is unused (`FELUCCA_CPU2 0`); it's a reserve for heavy DSP (see *0.5.1*).

**Rules of thumb**
- Per-track settings cost almost nothing. Per-step data multiplies: 64 patterns × 7 tracks × 64 steps means 1 bit per step ≈ 3.5 KB and 1 byte ≈ 28 KB. At 32 patterns, halve those.
- No floats, and no 64÷64 division on the target. Divisors must be 32-bit, or the link fails on `__udivdi3`/`__divdi3`. Felucca links no runtime library.
- Avoid constants between 0xFFC00000 and 0xFFCFFFFF (for example −4194304). build.py's ROM-address guard rejects them.
- After each change: `./build.sh`, the three host tests, and `web/emu/build.sh` for a browser check.

## Phase 1: headroom, sends, reverb (done in 0.2, except the CPU measurement on hardware)

What was built, and where it differs from the plan:
- **Projects** keep six 48 KB slots (four user slots plus AUTO), so 0.1 projects stay readable where they are. Version 2 stores a step as the fields that differ from an empty step. A step with velocity and note set takes 3 bytes instead of 8. In `ui_test`, a busy project (32 patterns, every track 32 steps, velocity and note on each step, every kit edited) takes 31 KB. A fully dense project (64 steps, every field, every kit parameter) still reports PROJECT TOO BIG. Fewer, larger slots are still an option.
- **AUTO save** used to fail silently when the session was too big, then retry every ~10 s, erasing 48 KB of flash each time. Now it shows AUTOSAVE: PROJECT TOO BIG for 6 s and does not retry until the session changes. A related bug that was already in 0.1 is fixed: saving a project that is too big wrote past a 256-byte buffer.
- **Kettle's new parameters** are numbered 302–317 (NRPN 174–189) and are stored in the kit after the LXR's sound parameters, so they morph and save with it. A step stores a lock as one byte, so locks on them use lock numbers the LXR leaves free (247–254 and its UNUSED 221–227). The reverb model can't be locked.
- **FX send** is a split, not an extra send: it sets how much of the voice goes through the bus, because the LXR's bus returns dry plus wet. At 127 it is the old Out = FX BUS (`ui_test` checks they are sample-identical). FX BUS is no longer an output choice.
- **Reverb**: ROOM's feedback now rounds toward zero. Felucca's ROOM leaves a DC offset of about −33 after each hit, which Felucca's master DC blocker hides and Kettle has none of. The reverb sleeps when it isn't fed and its tail is silent.

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
- **Locks**: 0.2's trick of reusing free lock numbers had run out. Each step now has one more byte holding the high bits of both locks' numbers (`lhi` in seq.c, ~15 KB for 32 patterns), so any of Kettle's parameters can be locked except the reverb model. Projects are version 3. Version-2 projects load, and their locks on the sends and reverb move to the new numbers.
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
- **GRIDS** (the optional map, added once its licence was checked: Grids' AVR code is GPL-3.0-or-later, compatible with Kettle's GPL-3.0-only). It is a fourth generator: a track takes a part (BD, SD, HH, defaulting from the track) with its own density and accent. The map's X, Y and chaos are the pattern's (the GRIDS MAP page), as on the module, so moving them moves every Grids track together. The 25 nodes come from Grids' `resources.cc` via `tools/gen_grids.py` (output committed as `firmware/src/grids_map.h`, 2.4 KB of flash). The map is read exactly as Grids reads it (checked at 2,700 points). Grids' bar is 32 32nd steps: a Kettle step reads its two, and a hit only on the second plays half a step late, using the step's shift. Chaos perturbs the levels once each time round, as Grids does. Project format version 5 stores the map.

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
- Kettle's USB name ("Kettle") wasn't in the updaters' port filter (`web/fm1ota.js`, `tools/fm1_install.py`), so neither could have found an FM-1 running Kettle to take it back to V15. Fixed: both match "kettle".
- The manual's recovery advice was wrong: UBOOT (OCT− + OCT+, the boot guard) is the chip's mask-ROM mode, and only the FM-1-transporter can write from it. Rewritten (MANUAL.md section 1).
- An FM-1 running another alternative firmware (Jiant) wasn't found: its USB name isn't one the updater probes. It went back to V15 with its own installer, as the page now advises; "not found" lists the MIDI ports the browser sees.
- On one computer the update loader ("Felucca Update", USB 1209:0002) never appeared after V15 handed over, not even in Audio MIDI Setup; turning the FM-1 off and on returned it to V15 unchanged. On another Mac it appeared within 40 s and the install finished. So: that computer's USB path, not the package (Kettle's loader is byte-identical to Felucca's).
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
- **Parameters** (`sound.c`): `PAR_MODEL1` .. `PAR_MTENS1`, six each like Kettle's other per-voice settings (4..6 unused), NRPN 256–297. Defaults (`kit.c`): decay 64, damping 40, material 64, position 40, exciter 32, tension 0. All lockable per step; Model is a menu and doesn't morph (`morph_excluded`). Old kits and projects load as LXR: the project stores `NPAR` and its count.
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

The user's list (October 24): the name and the repository, the physical models' sound, LXR-02 kits on the unit, backing up what's on the unit, and checking that Kettle has what makes people love the LXR-02, not only its engine.

**Name (decided):** Kettle stays. The LXR firmware's licence terms (its README and NOTICE.md, GPL-3.0 section 7(e)) don't allow "LXR" in a firmware's name without Erica Synths' written permission; truthful compatibility statements are fine. The code is at https://github.com/dmajka/kettle (`REPO` in `web/make_site.py`).

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

### Phase 7: the Kettle library — backup, restore and kit import over USB (built in 0.7)

What was built, and where it differs from the plan:
- **Device:** `firmware/src/library.c`, one entry point (`lib_in`) for a whole message, used by the firmware (`lib_service` in the main loop, before `ota_service`: a frame that isn't the library's is left for the updater), the emulator (`web_lib_in`, the worklet's "lib" message) and the host bridge. The protocol is in its header.
- **Commands:** besides the plan's, INFO also gives the loaded slots, KIT_NAMES lists the kits, and KIT_GET exports a kit. So the page shows and exports kits too.
- **Writes:** a restore goes to a free physical slot, with its header written last after the payload's CRC is checked in flash. While it is open, `proj_lock` holds off autosave and SAVE; it gives up after 10 s without data, and the output is hushed throughout.
- **Kits:** KIT_PUT takes any length up to KIT_LEN. An LXR .SND's 247 bytes get Kettle's defaults for the rest, `kit_sanitize`, and their names cleaned. LXR "Out = FX BUS" becomes FX SEND 127 (204 of 15,314 bytes in the factory PROJ00, all of them outputs).
- **Pages:** `web/library.html` (the site's `library/`) for the FM-1, and a Library panel on the emulator page. Backup files: `.ktbk` (several projects) and `.ktp` (one project's image as it is in flash). Both are self-checking (the header's CRC and the payload's).
- **Tests:** `tests/library_bridge.c` + `tests/library_test.mjs`:
  - the 62 factory kits in and back (byte for byte, apart from the FX bus outputs);
  - a backup read, restored into another slot (identical payload) and loaded;
  - a damaged image refused, data before BEGIN refused, SAVE refused during a restore and allowed after the 10 s timeout;
  - a garbled message unanswered.
  - Also checked in Chrome on the emulator: three Hrtl kits imported (the Init one skipped) and the first loaded.
- **Not done:** the transfer over a real FM-1's USB is untested (the SysEx path is the updater's, which works). The first hardware test should back up, restore into another slot and load.

Today nothing on the unit can be copied off it. Kettle's projects sit in flash at 0x97000–0xDEFFF. Kettle's own updates don't touch that area, but V15 or another firmware may, and nothing protects against a bad flash.

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
- **The bank:** `tools/gen_factory_kits.py` takes `KETTLE_KITS_DIR` as it is (':' between folders) and leaves out repeats by content. From the factory PROJ00, PROJ05 and PROJ01, Hrtl's UNTZ and DLC kits: 111 kits, about 28 KB of flash (image 323 KB against 294 KB).
- **On the unit:** `engine_init` fills the slots, so it only shows on a unit with no session. LOAD → FACTORY loads a kit into the edit buffer (the header still names the slot's kit until it's saved), or ALL > SLOTS.
- **Publishing guard:** `web/make_site.py` looks for "unofficial port, …+K" in `build/kettle.bin` and `build/emu/kettle.wasm`.
- **Tests:** checked in Chrome on a +K emulator: it starts with Badonka, the FACTORY page lists the bank, and a factory kit loads.

- `tools/gen_factory_kits.py KIT_DIR` reads .SND files from a local folder (the factory SD image's PROJ00, Hrtl's UNTZ and DLC kits) into `build/gen/factory_kits.h`, which is never committed.
- `KETTLE_KITS_DIR=… ./build.sh` (and `web/emu/build.sh`) builds them in. Without it, the build is as today.
- With the bank built in:
  - A fresh unit (no session) starts with the bank's first 64 kits in its slots.
  - LOAD → FACTORY copies them in again on request.
  - The version shows "+K".
- `web/make_site.py` refuses to publish a "+K" build.
- The import tool (Phase 7) is the public route.

### Phase 9: the LXR-02's live feel (built in 0.8)

What was built, and where it differs from the plan:
- **9a, LXR patterns:** the library page imports a whole LXR project folder: its kits into the slots by number, its .PAT patterns (V1, V2 and V3 formats) and .SNG songs. Kettle has 32 patterns, so the user picks the LXR's first or second half; a song step on the other half plays pattern 32. New SysEx: PAT_BEGIN / PAT_STEPS / PAT_END (0x32–0x34), SONG_PUT (0x35).
- **9b, MIX in PERF, and the page order** (the user asked whether performance, macros, more performance was confusing; it was): PERFORM (BPM, Roll, Morph, SRate: the LXR's PERFORMANCE page), PERFORM: MORE (DJ filter, Repeat, Shuffle, Tape), PERFORM: MIX (D1–SN volumes), MIX 2 (CY, HH volumes, FX dry/wet, reverb level), MACROS, MACRO SETUP, then PERFORM 2 and its setups. A held roll puts the swept roll on KNOB 2.
- **9c, KEYS:** SHIFT + EDIT. The keys play the track's voice from F (octave on KNOB 1); notes go out on MIDI and into the steps under REC (as MIDI notes in, through the audio side's queue). The sound pages opened from KEYS keep the keys playing.
- **9d, key velocity:** KEYS' KNOB 2 and 3: the velocity of keys, rolls and auditions, and the accent (SHIFT). Kept in the settings.
- **9e, sound browser:** PRESETS on a sound page or in KEYS: the library's sounds, then the voices of the library's kits, the slots' kits and the factory kits (a drum track: all three drums of each; init voices left out). Each step auditions; OCT+ keeps, OCT− reverts.
- **9f, kit library:** `firmware/src/kitlib.c`. 128 kits and 128 sounds in flash at 0xEA000–0xFBFFF (18 sectors: a log of 256-byte pages, the commit record written last, one sector kept erased for compaction). SAVE / LOAD → KIT and MORPH take To / From SLOTS or LIBRARY; SAVE / LOAD → SOUND keeps one voice (a drum's into any drum). The library page lists, backs up (`.ktlb`), restores and imports LXR kits into it (SysEx KL_LIST / KL_GET / KL_PUT, 0x40–0x42). Not yet checked on a unit: that stock V15 and the installer leave 0xEA000–0xFBFFF alone (the stock store is 0x93000–0xE8FFF, and Kettle's updater doesn't write there).
- **9g, SRate:** on PERFORM's KNOB 4 ("SRate"), lockable, so it is a macro target and records under REC.
- **Tests:** ui_test phase9 (PERF pages, MIX, KEYS, the library's SAVE / LOAD, the browser), library_test sections 7 and 8 (an LXR project in, V2 and V3; the kit library: list, get, put, delete, backup, restore, a reboot's scan).
- **Still open:** the user's comparison with LXR-02 videos (what still feels missing).


The audit (Kettle against the LXR-02 manual and firmware):
- **Already ported:** every voice parameter, per-voice LFOs (synced, retriggered from any track, any target), velocity to any parameter, two locks a step, knob recording, rolls (recorded under REC; a single hit when stopped), morph, global sample rate, shuffle, mutes with PERF's unmute-all, sound editing from PERF with the keys still playing, kit change with patterns, reload, songs, Euclid, chromatic MIDI play (note ANY), step notes from a MIDI keyboard.

What's missing is mostly what you meet first:
- **The kits:** the LXR-02's sound in demos is hrtl's kits, which lean on LFOs, velocity routing and locks. Kettle starts on the init kit. (Phases 7–8.)
- **Demo patterns:** the factory and DLC projects come with patterns.
- **Faders:** six volume faders, ridden while playing. The FM-1 has four knobs and the mixer is three presses away.
- **Playing it:** the LXR's voices are played melodically from MIDI. The FM-1 is a keyboard, but Kettle's keys are steps.

Planned:
- **9a. LXR patterns:** .PAT import in the library, so a whole LXR project (kits, patterns, songs) becomes a Kettle project. The demo patterns come with it.
- **9b. MIX in PERF:** a PERF page with the voices' volumes (two pages: D1 D2 D3 SN / CY HH, FX return, reverb return), so levels can be ridden like the faders.
- **9c. KEYS:** the white and black keys play the selected voice chromatically, as an instrument; REC writes the notes into steps (note and velocity), as MIDI notes do now.
- **9d. Key velocity:** the FM-1's keys have no velocity. A setting gives the velocity of the keys and rolls, and SHIFT plays an accent.
- **9e. Sound browser (per track):** select a track (say D1) and browse, audition and load single-voice presets taken from every kit's voices of the same kind: the D1, D2 and D3 voices of all kits for a drum track, SN for SN, CY for CY, HH for CH/OH. Save the current track's sound as a preset too. Not in the LXR-02; it works like the Digitone's or Digitakt's sound browser. (Requested 2026-10-24.)
- **9f. A kit library in flash, and morph from anywhere:** more of the user's own kits without more RAM. The 64 RAM slots stay as they are (the pool has ~18 KB free and keeps 8 KB spare; Phase 10 should free ~7 KB, which isn't spent on kits). Saved kits and 9e's single-voice presets go to a store in flash outside the projects, read one at a time as FACTORY is. About 108 KB after the OTA area (0xE5000–0xFFFFF) looks unused, room for ~250 kits; first confirm stock V15 and the installer leave it alone. LOAD and LOAD MORPH take a kit from a slot, FACTORY or the library: morph needs only the playing kit and the target in RAM (0.7 KB). The library page backs the store up. (Requested 2026-10-24: "let's not use up all of our RAM on kits".)
- **9g. The global sample-rate reducer, easy to reach:** the LXR's PAR_VOICE_DECIMATION_ALL (its PERFORMANCE page: BPM, Roll, Morph, Sample rate, Shuffle) is ported exactly (voices.c, mixer_decimateBlock: each voice's own rate × the global one, before the mix), but it is hidden on PERFORM: MORE, KNOB 3 ("All SRate"). The LXR's global crush is a sample-rate reducer, not a bit-depth one. Kettle's extras are separate: per-voice Bits (Shape page) and PERF 2's CRUSH effect on the output. To do: put it on a main PERF page next to Morph (the LXR's own layout), check that it is a macro target and that knob recording records it, and check it against the LXR's sound (it is a kit parameter there, so a kit carries it). (Requested 2026-10-24.)
- After 9: the user compares with LXR-02 videos and lists what still feels missing.

### Phase 10: Rings in place of the physical models (built in 0.9)

Requested 2026-10-24: the user isn't fond of how the Phase 5d/6 physical models sound.

What was built:
- **The port:** `firmware/src/rings.c`, a fixed-point port of Rings' modal resonator (`resonator.cc`), string (`string.cc`, with its dispersion, curved bridge and the plucker) and FM voice (`fm_voice.cc`, with its follower), Part's exciter filter and its limiter, with the stmlib DSP they use. Its tables come from `tools/gen_rings_tables.py`. Rings is **MIT-licensed** (not GPL as first noted); its notice is in `LICENSES/MIT-Rings.txt` and NOTICE.md.
- **Models:** LXR, MODAL, STRING, FM on Drum 1–3. The knobs are Rings': Decay (DAMPING), Bright (BRIGHTNESS), Struct (STRUCTURE), Position (POSITION), plus Exciter (Rings' own exciter at 0, the voice's LXR sound as Rings' audio input at 127, a mix between) and Tension.
- **Left out:** the sympathetic strings (several strings a voice: CPU), Rings' other modes, its reverb (Kettle has its own) and its polyphony (a drum voice is one).
- **Cost:** MODAL runs 32 modes with one drum on it, 24 with two, 16 with three (Rings: 60, 28, 12 by polyphony). The string's lines are 32-bit and live in fast RAM (18 KB for three voices): in 16 bits, a long decay's tail sticks at a level. The line is 1024 samples (43 Hz); lower notes run the string at a lower rate, as Rings' does. CPU on the FM-1 not yet measured: the first thing to check on hardware, with three drums on MODAL.
- **Accuracy:** `tests/rings_compare.cc` (with `tests/rings_core.c`; `tests/rings_compare.sh` needs Rings' and stmlib's sources) renders 18 cases (pitches from 30 Hz to 1 kHz, every model, their parameters, impulses, bursts and the FM envelope) with the port and with Rings' float code at 44.1 kHz: correlation 0.9998 and up, the error −35 dB or lower. Where Q20 alone wasn't enough, the port keeps more bits (the modes' rounding error fed back, the SVFs' and DC blockers' Q26 states, the FM envelopes' Q28, a finer ratio for the FM modulator, a true sine table: the LXR's is off by half a step), and replicates stmlib's 1/256-semitone pitch steps.
- **The old models:** their code is kept, unbuilt, in `firmware/src/physmodels.c`. Projects (version 7 now) from before map SKIN, BAR, BELL and PLATE to MODAL and TUBE to STRING (`proj_models_08`).
- **Tests:** ui_test phase5d (MODAL's harmonic and stretched partials, STRING's and FM's pitch, STRING below its line, DECAY, the level over models, BRIGHT, STRUCTURE and pitch, EXCITER, velocity, D1 through the voice, TENSION, the page, a project, version 6's mapping).
- **Next:** the user listens, and the CPU is measured on a unit.

## Later / bigger

- **303-style acid track:** about 33 KB of pattern memory at 64 patterns (less at 32), 1–2 voices' worth of CPU, and real UI work for track selection and note entry. Check charlesvestal's fm1-x0x (FM-1 303 firmware) and its licence first.
- **More FX buses:** cheap for drive, ring mod or compressor; costly if each has its own delay.
- **LXR-02 preset import:** Kettle uses the LXR's own parameter numbering. Either a computer script that sends a kit as CC/NRPN, or a converter to a Kettle project plus a USB transfer path. Check the LXR SD file format (lxr-desktop-app may read it).
- MIDI CC out when the panel's knobs turn.
