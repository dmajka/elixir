# Kettle

The Erica Synths LXR-02 drum machine's firmware, ported to the M-VAVE FM-1.

Kettle runs the LXR-02's sound engine and sequencer on the FM-1. It has six voices on seven tracks (three drums, snare/clap, cymbal, closed and open hat), the FX bus, 32 patterns of 4 bars with parameter locks, flams and shifts, rolls, Euclid, morph, songs, kits and projects, and MIDI in/out with clock sync. On top of the LXR, each voice has an FX send and a reverb send, and the reverb is Felucca's (room, spring, hall). Each voice also has a waveshaper choice, a bit reducer, a tilt EQ, random-per-hit variation, a choke group and cross-modulation from another voice. The FX bus adds a phaser and a flanger. The three drum voices can also ring through a port of Mutable Instruments Rings' resonators (modal, string, FM). The keyboard can play a voice as an instrument (KEYS), a sound browser loads single sounds into a track, and a kit library in flash holds 128 kits and 128 sounds beyond the project. It is built on [Felucca](https://github.com/hugelton/Felucca)'s hardware layer, USB stack, updater, reverb and emulator.

The FM-1 has no FPU, so the LXR's floating-point DSP is rewritten in fixed point. `tests/compare_test.c` renders each voice with both engines and compares them: levels match to 0.1 dB, and the tonal voices correlate at 0.99–1.00. Noise-based sounds correlate lower only because the noise generator differs.

**Unofficial.** Not affiliated with or endorsed by Erica Synths, M-VAVE or Hügelton Instruments.

**[MANUAL.md](MANUAL.md)** documents everything: the controls, every page, and what Kettle adds to the LXR-02.

## Controls

| FM-1 control | Kettle |
|---|---|
| White keys 1–16 | Steps of the shown bar · patterns (pattern page) · rolls, FILL and effects (PERF) |
| Black keys 1–7 | Tracks D1 D2 D3 SN CY CH OH (select) · SHIFT + key: mute |
| Black keys 8–11 | Bars 1–4 (shown bar) · banks of 16 (2 on the pattern page, 4 for kits and songs) · SHIFT: track length · COPY: copy the shown bar into it (on the grid: every track) |
| OCT− | **SHIFT** (hold it: the screen shows every SHIFT combination) |
| OCT+ | **COPY** (hold + track/bar/pattern) · SHIFT + OCT+ = **CLEAR** · OK in dialogs |
| EDIT | Oscillator → FM → transient (click) → model (drums) · SHIFT + EDIT: **KEYS** (the keys play the track's voice) |
| ENV | Amp EG → mod EG / velocity |
| SCL | Filter → shape (waveshaper, drive, bits, tilt) |
| LFO | LFO → random per hit → cross mod / choke |
| GLO | Mix → mixer (volume, pan, FX send, reverb send) → config (MIDI, sync; CPU load at the bottom) |
| FX | FX bus: drive (tube/fold/rat), ring mod, compressor, delay, phaser, flanger → reverb (SELECT: its page) |
| SEQ | Step edit: velocity, note, probability, condition · flam, shift · locks · ratchet |
| ARP | PERF (rolls, effects, mutes, DJ filter, macros) → Euclid / generator / track clock |
| HOME | Grid → patterns → song |
| SAVE | Save → load (kit, pattern, song, morph, project) |
| PLAY / REC | Start/stop (SHIFT + PLAY: the song from the top) · record (SHIFT + REC: options) |
| ALGORITHM | Track |
| SELECT | Page / list position |
| PRESETS | Kit browser (OCT+ loads) · on a sound page or in KEYS: the sound browser (OCT+ keeps, OCT− reverts) |
| KNOB 1–4 | The four parameters on screen. **Hold a step and turn a knob** to lock that parameter on that step |

SHIFT + white keys follow the LXR's SHIFT + step functions: 1–7 audition a voice, 8 KIT, 9 MORPH, 10 SONG, 11 PROJECT, 12 CONFIG, 13 RELOAD, 14 LAST (last-touched parameters), 15 FX, 16 PATTERN.

## Sends and reverb

Each voice has two sends, set on the mixer's FX SEND and REVERB SEND pages (GLO → mixer, SELECT):

- **FX send** sets how much of the voice goes through the FX bus instead of straight to its output. At 127 the whole voice goes through the bus. This is the LXR's Out = FX BUS, so kits and projects that used FX BUS load as FX send 127 with the output set to STEREO, and sound the same.
- **Reverb send** feeds the voice into the reverb, on top of its output.

The reverb is a send bus after the FX bus. Its page is the third FX page: model (ROOM, SPRING, HALL), size, damping and return level. The return level is also on the second mixer pan page. Sends and reverb settings are part of the kit, morph with it, and can be locked per step (hold a step and turn the knob on the mixer page). When nothing has been sent to it for about 0.4 s and its tail is silent, the reverb stops processing until the next send.

## Copying a bar

On the grid (HOME), hold COPY (OCT+) and press a bar key. The bar you are looking at is copied, every track, into that bar. A track that ended before the new bar grows to include it, unless its length isn't a whole number of bars (a 12-step track keeps its 12). Then move to the new bar and change what you want. On the other pages (a sound page, step edit), COPY + bar key copies only the selected track.

## Steps and tracks

**Per step** (SEQ, then SELECT for the pages):
- **Condition** (STEP page): when the step plays. A:B plays the A-th time of every B times round (1:2, 2:4, 3:8 ...). FILL plays only while PERF's FILL key is held, !FILL only while it isn't. PRE plays if the track's last condition was true, NEI if the previous track's was. 1ST plays the first time round after PLAY. The ! versions are the opposite. A step's chance still applies on top.
- **Ratchet** (RATCHET page): 2–8 hits from the step, at 1/32, 1/48, 1/64 or 1/24, with the velocity FLAT, fading DOWN or rising UP. A ratchet can run past its step.
- On the grid, a condition shows as a dot top left, a ratchet as a bar under the step.

**Per track** (ARP twice: the EUCLID pages, SELECT for the next):
- **Euclid**: length, hits, rotation as before. **Action**: pick DICE (the track again, with the Euclid hits placed at random), COMMIT, UNDO or RESET, then press OCT+.
- **Generator**: plays a live copy of the track and changes it as it plays. The pattern itself stays as it was until COMMIT copies the live track into it. UNDO takes back the last DICE, COMMIT or Euclid change; pressing it again redoes it. RESET copies the pattern into the live track again, as turning a generator on or changing pattern does.
  - MUTATE: amount, freeze. Each time round, up to two changes: a step flipped, a step moved, a lock's (or the velocity's) value drifted.
  - TURING: length (the register, 2–16), lock (0 random … 127 repeats), density (how many steps play).
  - CELLULAR: rule (Wolfram's 0–255: 30, 90, 110 …), freeze. Each time round the steps become the rule's next row.
  - GRIDS: Emilie Gillet's Grids drum map. Settings: part (BD, SD, HH), density, accent (the velocity follows the map). The map's X, Y and chaos are on the GRIDS MAP page and belong to the pattern, so turning them moves every Grids track together. Put the kick on BD, the snare on SD and a hat on HH, then explore the map.
- **Grids map** (4th page): X, Y, chaos.
- **Track clock**: the track's step rate against the pattern: /4, /3, /2, 2/3, 3/4, 1x, 4/3, 3/2, x2, x3, x4. Use it for polyrhythms, or with lengths for polymeters. The pattern lasts as long as its longest track.

## Performance (PERF)

Press ARP for PERF. The white keys act while held, and SHIFT + a key latches it on (the key again turns it off). Leaving PERF (HOME, SEQ) turns off whatever is latched.

| Keys | |
|---|---|
| 1–7 | Roll the track at the roll rate (as on the LXR), at the KEYS velocity |
| 8 FILL | Every step plays, whatever its probability |
| 9 RPT | **Repeat:** the sequencer loops its position at the repeat length (1/4, 1/8, 1/16, 1/32: the bar keys or the Repeat knob), starting on that grid. When you let go, the pattern carries on where it would have been. |
| 10 FRZ | **Freeze:** the FX bus delay loops what it holds (input off, feedback 100%). It needs the FX bus set to DELAY; the longest loop is about 0.74 s. |
| 11 T.RV | **Throw:** the selected track's reverb send goes to 127 while held, and the tail rings out after. The ALGORITHM encoder selects the track. |
| 12 T.FX | **Throw** to the FX bus (FX send 127) |
| 13 FB+ | **Feedback push:** delay feedback to 0.95 (phaser and flanger to their maximum) |
| 14 STP | **Tape stop:** tempo and every voice's pitch slow to a stop over the tape time (0.2–4 s), then go silent. When you let go, they spin back up in 0.25 s. |
| 15 LP~ / 16 HP~ | **Sweeps:** push the DJ filter to its low-pass or high-pass end over a bar, and back in a quarter bar after you let go |
| Black keys | Mutes · bar keys: repeat length |

The knobs have nine pages, selected with SELECT: the performance pages, then the macros, then PERFORM 2.

- **Perform** (the LXR's PERFORMANCE page): tempo, roll rate, morph, and **SRate**, the LXR's global sample-rate reducer (each voice's own rate times this one; part of the kit). While a roll is held, the roll knob sweeps its rate smoothly from 1/4 to 1/192.
- **More:** DJ filter (off at the middle; left a low-pass on the master, right a high-pass), repeat length, shuffle, tape time.
- **Mix** and **Mix 2:** the six voices' volumes, the FX bus's dry/wet and the reverb's level, to ride like the LXR's faders.
- **Macros:** four knobs. **Macro setup:** pick a macro, its target (any parameter a step can lock), and its min and max. A max below the min turns the macro the other way. Turning a macro under REC records locks, as any knob does.
- **PERFORM 2** and its two setup pages: a second bank of key effects (drill, half time, jitter, cuts, short, pump, comb, space, crush, reverse); see the manual.

Macros, the repeat length and the tape time are saved with the project.

## Sound shaping

Each voice (the open and closed hats share one) has three extra pages. All of their settings are part of the kit, morph with it, and can be locked per step.

- **Shape** (SCL, press again after the filter): the **waveshaper** used by the voice's drive: LXR (the original curve), FOLD, CLIP, ASYM (soft on top, clipped below), RECTIFY (full-wave, an octave up). Also the **drive** itself, **bits** (OFF, then 15 down to 1; applied after the sample-rate reducer) and **tilt** (0 is flat; + brighter, − darker).
- **Random per hit** (LFO, after the LFO's two pages): how much each hit varies in **pitch** (up to an octave either way at 127; small amounts are subtle), **decay** (half to double), **filter** cutoff and **pan**.
- **Cross mod / choke** (LFO, next): another voice as the **source** and a **mode**. DUCK turns this voice down while the source sounds, FILTER opens this voice's filter with the source's level, and RING multiplies the two. **Amount** sets how much. **Choke** puts the voice in a group from 1 to 4. A hit on any voice in the group fades the others out within 1.5 ms.

Drum 1–3 also have **models** (EDIT, after the transient: MODEL), ported from Mutable Instruments **Rings** (MIT licence) to fixed point: **MODAL** (Rings' modal resonator, up to 32 modes: bars, plates, bells, drum heads), **STRING** (its string, with dispersion and a curved bridge) and **FM** (its two-operator FM voice). The knobs are Rings': **decay** (DAMPING), **bright** (BRIGHTNESS), **struct** (STRUCTURE), **position** (POSITION), plus **exciter** (0: Rings' own exciter at the hit; 127: the voice's own LXR sound, as if patched into Rings' input) and **tension** (the pitch drop of a hard hit). The voice's filter, shape and sends still apply. Rings' sympathetic strings and reverb aren't ported. `tests/rings_compare.cc` checks the port against Rings' own code. Projects from 0.8 and before load their old models (SKIN, BAR, BELL, PLATE, TUBE) as MODAL or STRING.

**KEYS** (SHIFT + EDIT): the white and black keys play the selected track's voice chromatically (KNOB 1: octave). Under REC the notes go into the steps. The keys have no velocity, so KNOB 2 sets it (for rolls too) and KNOB 3 the accent, played with SHIFT.

**Sound browser:** on a track's sound pages, turn PRESETS to step through the sounds that track can take (the library's sounds, then the voices of the library's kits, the kit slots and the factory bank), each auditioned; OCT+ keeps one, OCT− goes back. SAVE → SOUND saves a track's sound to the library.

The FX bus has two new types. **Phaser**: rate, depth, feedback, centre. **Flanger**: rate, depth, feedback (negative below the middle) and delay. Both sound strongest at DRY/WET 50%.

## MIDI and sync

- **In:** USB MIDI and the TRS jack. Notes on each track's channel play its voice; the global channel plays the selected track. CC *n* sets parameter *n*−1 and NRPN reaches the rest, as on the LXR. CC0 picks a kit, program change 0–31 a pattern.
- **Kettle's own parameters** (NRPN, six numbers per voice-based setting, in the order D1 D2 D3 SN CY HH): 174–179 FX send, 180–185 reverb send, 186 reverb size, 187 damping, 188 level, 189 model (0 room, 1 spring, 2 hall), 190–195 bits, 196–201 waveshaper, 202–207 tilt, 208–213 choke group, 214–219 random pitch, 220–225 random decay, 226–231 random filter, 232–237 random pan, 238–243 cross-mod source (0 none, 1–6), 244–249 cross-mod mode (0 duck, 1 filter, 2 ring), 250–255 cross-mod amount.
- **Out:** USB MIDI. The FM-1 has no MIDI out jack.
- **Clock out:** always sent, 24 PPQ, plus start/stop.
- **Clock in:** turn the tempo below its minimum to **MIDI**. Kettle then follows incoming clock and start/continue/stop.

## Try it in the browser

```sh
tools/gen.sh           # generated headers (needs Python 3 with Pillow and fontTools)
web/emu/build.sh       # needs Emscripten -> build/emu/
cd build/emu && python3 -m http.server 8765    # open http://localhost:8765
```

The emulator runs the firmware's own C sources compiled to WebAssembly, on a simulated FM-1. Use the computer keyboard or the mouse. Enable Web MIDI to play it from a controller and to send its clock to other apps.

## Library: backup, restore, LXR-02 kits

`web/library.html` (the site's `library/`) talks to Kettle over USB MIDI SysEx (`firmware/src/library.c`, protocol in its header; `web/library.js`). It backs up and restores projects, imports LXR-02 .SND kits, imports a whole LXR-02 project folder (kits, patterns in V1–V3 formats, songs; Kettle's 32 patterns take one half of the LXR's 64), and backs up and restores the kit library (`.ktlb`); the emulator page has the same under **Library**.

**Kit library** (`firmware/src/kitlib.c`): 128 kits and 128 single sounds in flash at 0xEA000–0xFBFFF, outside the projects, read one at a time (no RAM per kit). SAVE / LOAD → KIT and MORPH reach it with KNOB 3 (To / From LIBRARY), SAVE / LOAD → SOUND saves or loads one track's sound, and the sound browser lists it. Writes are a log with a commit record written last, so a write cut short leaves the slot as it was. That flash area is outside Kettle's updater and the stock firmware's known store; that the stock V15 leaves it alone is not yet confirmed on a unit, so back the library up before installing other firmware.

The LXR-02's factory and Hrtl kits can't be redistributed, but a private build can include your own copies: `KETTLE_KITS_DIR=dir:dir ./build.sh` (and `web/emu/build.sh`) generates `build/gen/factory_kits.h` (`tools/gen_factory_kits.py`). The version gets "+K", a fresh unit starts with those kits, LOAD has a FACTORY type, and `web/make_site.py` refuses to publish the build.

## Build the firmware

Same requirements as Felucca: the JieLi pi32v2 toolchain (`~/.jieli/toolchain`), the JieLi AC79 SDK (`~/fw-AC79_AIoT_SDK`), Python 3 with Pillow and fontTools, and Docker on macOS (the toolchain is Linux x86-64).

```sh
PYTHON=/path/to/python ./build.sh       # -> build/kettle.fwsc
```

## Install

`web/make_site.py` makes Kettle's site in `build/site` (after `./build.sh` and `web/emu/build.sh`): the install page (Web MIDI, Chrome or Edge: a device check, official V15 first, Kettle, and back to V15), the emulator (`try/`), the manual as a page (`manual/`), and the package, source and licences (`firmware/`). Any static host serves it. For GitHub Pages, `tools/publish_site.sh` builds it and commits it to the `gh-pages` branch (then `git push origin gh-pages`; Settings → Pages → Deploy from a branch: `gh-pages`, root). The source is at https://github.com/dmajka/kettle (`REPO` in `web/make_site.py`; the pages link it). Install the official V15 first (see the manual's section 1).

Or install `build/kettle.fwsc` over USB:

```sh
python3 tools/fm1_install.py build/kettle.fwsc
```

To go back, install the official firmware file (`fm1_install.py FM-1.fwsc`) or any other alternative firmware. Kettle stores its projects and settings in the same flash region Felucca uses for its own data, so installing Kettle replaces data saved by Felucca.

Projects saved by older versions load in newer ones. Kettle 0.1 had 64 patterns; newer versions keep the first 32, and songs that pointed past them play pattern 32 instead. Projects do not load in older versions than the one that saved them.

## Tests

```sh
cc -O2 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal tests/render_test.c -o build/render_test && build/render_test build/
cc -O1 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal tests/ui_test.c -o build/ui_test -lm && build/ui_test build/shots
```

```sh
cc -O1 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal tests/library_bridge.c -o build/library_bridge
node tests/library_test.mjs [LXR project folder]   # the library page's code against the device's
```

```sh
EURORACK=path/to/eurorack STMLIB=path/to/stmlib tests/rings_compare.sh   # the models against Rings' own code
```

`render_test` plays every voice and counts the MIDI clock. `ui_test` drives the panel as a user would (programming a beat, sound pages, locks, patterns, songs, saving and loading a project, MIDI sync in) and writes a screenshot of each screen to `build/shots/`. It also checks the sends, the reverb, the sound shaping, the phaser and flanger, loads a Kettle 0.1 project (`tests/data/proj_v1.bin`), and prints how big a busy and a dense project get. `compare_test` checks the voices against the original LXR DSP sources and needs the LXR repository (see its header). `library_test` runs the library page's own code against the device's through `library_bridge`: kits in and out, backups, restores, LXR projects (the LXR kits from `../lxr-02_kits` if there, or the project folder given), and the kit library. `rings_compare` renders the models with Kettle's fixed-point port and with Rings' float code (github.com/pichenettes/eurorack and stmlib, not in this tree; the script sets their sample rate to Kettle's 44.1 kHz in a copy) over pitches and settings, and fails below a correlation of 0.99 or above an error of −30 dB.

## Licence

GPL-3.0-only. See [NOTICE.md](NOTICE.md) and `LICENSES/`.
