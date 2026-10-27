# Elixir

The Erica Synths LXR-02 drum machine's firmware, ported to the M-VAVE FM-1.

Elixir runs the LXR-02's sound engine and sequencer on the FM-1. It has six voices on seven tracks (three drums, snare/clap, cymbal, closed and open hat), the FX bus, 32 patterns of 4 bars with parameter locks, flams and shifts, rolls, Euclid, morph, songs, kits and projects, and MIDI in/out with clock sync. On top of the LXR, each voice has an FX send and a reverb send, and the reverb is Felucca's (room, spring, hall). Each voice also has a waveshaper choice, a bit reducer, a tilt EQ, random-per-hit variation, a choke group and cross-modulation from another voice. The FX bus adds a phaser and a flanger. The three drum voices can also ring through a port of Mutable Instruments Rings' resonators (modal, string, FM) or play Plaits' sixteen original engines, two at a time (experimental: not in the LXR-02). The FM-1's second core renders the drum voices. The keyboard can play a voice as an instrument (KEYS), a sound browser loads single sounds into a track, and a kit library in flash holds 128 kits and 128 sounds beyond the project. It is built on [Felucca](https://github.com/hugelton/Felucca)'s hardware layer, USB stack, updater, reverb and emulator.

The FM-1 has no FPU, so the LXR's floating-point DSP is rewritten in fixed point. `tests/compare_test.c` renders each voice with both engines and compares them: levels match to 0.1 dB, and the tonal voices correlate at 0.99–1.00. Noise-based sounds correlate lower only because the noise generator differs.

**Formerly Kettle.** Elixir was called Kettle up to 0.13 (github.com/dmajka/kettle). A unit running Kettle updates to Elixir from the install page and keeps its projects, kit library and settings (see the manual's *Upgrading from Kettle*).

**Unofficial.** Not affiliated with or endorsed by Erica Synths, M-VAVE or Hügelton Instruments.

**[MANUAL.md](MANUAL.md)** documents everything: the controls, every page, and what Elixir adds to the LXR-02.

## Controls

| FM-1 control | Elixir |
|---|---|
| White keys 1–16 | Steps of the shown bar · patterns (pattern page) · rolls, FILL and effects (PERF) |
| Black keys 1–7 | Tracks D1 D2 D3 SN CY CH OH (select) · SHIFT + key: mute · SHIFT + GLO: solo the selected track |
| Black keys 8–11 | Bars 1–4 (shown bar) · banks of 16 (2 on the pattern page, 4 for kits and songs) · SHIFT: track length · COPY: copy the shown bar into it (on the grid: every track) |
| OCT− | **SHIFT** (hold it: the screen shows every SHIFT combination) |
| OCT+ | **COPY** (hold + track/bar/pattern) · held on a sound page + a knob: **CONTROL ALL** · SHIFT + OCT+ = **CLEAR** (on a sound page: CLEAR SOUND, the voice back to init) · OK in dialogs |
| EDIT | Oscillator → FM → transient (click) → model (drums) · SHIFT + EDIT: **KEYS** (the keys play the track's voice) |
| ENV | Amp EG → mod EG / velocity |
| SCL | Filter → shape (waveshaper, drive, bits, tilt) |
| LFO | LFO → random per hit → cross mod / choke |
| GLO | Mix → mixer (volume, pan, FX send, reverb send) → config (MIDI, sync; CPU load at the bottom; LOAD: the load by part and the cores) |
| FX | FX bus: drive (tube/fold/rat), ring mod, compressor, delay, phaser, flanger → reverb (SELECT: its page) |
| SEQ | Step edit: velocity, note, probability, condition → flam, shift → locks → ratchet (SEQ again: the next page) |
| ARP | PERF (rolls, effects, mutes, DJ filter, macros) → generator / action / track (length, clock, direction) / bounce / links |
| HOME | Grid → patterns → song |
| SAVE | Save → load (kit, pattern, song, morph, project) |
| PLAY / REC | Start/stop (SHIFT + PLAY: the song from the top) · record (SHIFT + REC: options) |
| ALGORITHM | Track |
| SELECT | Page / list position |
| PRESETS | Kit browser (KNOB 1: from the slots or the kit library; OCT+ loads) · on a sound page or in KEYS: the sound browser (OCT+ keeps, OCT− reverts) |
| KNOB 1–4 | The four parameters on screen. **Hold a step and turn a knob** to lock that parameter on that step |

Patterns keep their edits when you switch, as on an Elektron box: they are part of the session, which AUTOSAVE keeps. The pattern page's Edits setting can drop them instead, the LXR's way. RELOAD reverts the playing pattern either way.

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

**Per track** (ARP twice: the GENERATOR page, SELECT for the next):
- **Generator** (KNOB 1): OFF, EUCLID, MUTATE, TURING, CELLULAR or GRIDS; KNOBS 2–4 are its settings.
  - EUCLID: length, hits, rotation. It writes the track's steps, as the LXR's Euclid does. It starts at 0 hits over the track's length and changes nothing until the hits are turned. A second pulse (the ACTION page: Hits 2, Rotate 2) over the same length joins the first by OR, XOR, AND or SUB.
  - The others play a live copy of the track and change it as it plays. The pattern itself stays as it was until COMMIT copies the live track into it. EUCLID then MUTATE mutates the Euclid rhythm.
  - MUTATE: amount, freeze. Each time round, up to two changes: a step flipped, a step moved, a lock's (or the velocity's) value drifted.
  - TURING: length (the register, 2–16), lock (0 random … 127 repeats), density (how many steps play).
  - CELLULAR: rule (Wolfram's 0–255: 30, 90, 110 …), freeze. Each time round the steps become the rule's next row.
  - GRIDS: Emilie Gillet's Grids drum map. Settings: part (BD, SD, HH), density, accent (the velocity follows the map). Put the kick on BD, the snare on SD and a hat on HH, then explore the map.
- **Action** (2nd page): pick one with KNOB 1, the title says PRESS OCT+ TO …, press OCT+. EUCLID has DICE (its hits placed at random) and UNDO; the live generators COMMIT, UNDO and RESET (the live track from the pattern again, as turning a generator on or changing pattern does). UNDO takes back the last DICE, COMMIT or Euclid change; again, it redoes it. On a GRIDS track the page also has the map's X, Y and chaos, which belong to the pattern, so turning them moves every Grids track together.
- **Track** (3rd page): length; clock, the track's step rate against the pattern (/4, /3, /2, 2/3, 3/4, 1x, 4/3, 3/2, x2, x3, x4) for polyrhythms, or with lengths for polymeters (the pattern lasts as long as its longest track); **direction**: FWD, REV, PING (ping-pong), PING2 (the ends played twice), RAND (any step), WALK (a step either way). A time round stays the track's length in steps, so conditions and generators keep their timing.
- **Bounce** (4th page): the bouncing ratchets' first gap and pitch drift.

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

The knobs have thirteen pages, selected with SELECT: the performance pages (PERFORM, MORE, MIX, MIX 2, then PERFORM 2 and its setups), then the global LFOs, then the macros.

- **Perform** (the LXR's PERFORMANCE page): tempo, roll rate, morph, and **SRate**, the LXR's global sample-rate reducer (each voice's own rate times this one; part of the kit). While a roll is held, the roll knob sweeps its rate smoothly from 1/4 to 1/192.
- **More:** DJ filter (off at the middle; left a low-pass on the master, right a high-pass), repeat length, shuffle, tape time.
- **Mix** and **Mix 2:** the six voices' volumes, the FX bus's dry/wet and the reverb's level, to ride like the LXR's faders.
- **Macros:** four knobs. **Macro setup:** pick a macro, its target (any parameter a step can lock), and its min and max. A max below the min turns the macro the other way. Turning a macro under REC records locks, as any knob does.
- **Global LFO 1 and 2** (two pages each): an LFO on the whole kit's performance controls: morph, the DJ filter (both ways, or towards LP or HP), feedback push, comb, crush, jitter, cuts or pump. Depth −127 to +127; speed from 1/16 to 256 bars at the tempo; the LFO waves; mode FREE, TRIG, HOLD, ONE (one-shot) or HALF, triggered by PLAY, each pattern change or each bar (or OCT+ on its page). A one-shot saw on morph over 64 bars turns one kit into another while you play. The knobs stay where you left them; the LFOs move the sound.
- **PERFORM 2** and its two setup pages: a second bank of key effects (drill, half time, jitter, cuts, short, pump, comb, space, crush, reverse); see the manual.

Macros, the repeat length, the tape time and the global LFOs are saved with the project.

## Sound shaping

Each voice (the open and closed hats share one) has four extra pages. All of their settings are part of the kit, morph with it, and can be locked per step.

- **Shape** (SCL, press again after the filter): the **waveshaper** used by the voice's drive: LXR (the original curve), FOLD, CLIP, ASYM (soft on top, clipped below), RECTIFY (full-wave, an octave up). Also the **drive** itself, **bits** (OFF, then 15 down to 1; applied after the sample-rate reducer) and **tilt** (0 is flat; + brighter, − darker).
- **LFO mode** (LFO, after the LFO's two pages): TRIG (the LXR's: a hit restarts it), HOLD (runs free, its value held at each hit), ONE (one cycle from a hit, then still: an envelope) or HALF, as on Elektron's machines.
- **Random per hit** (LFO, next): how much each hit varies in **pitch** (up to an octave either way at 127; small amounts are subtle), **decay** (half to double), **filter** cutoff and **pan**.
- **Cross mod / choke** (LFO, next): another voice as the **source** and a **mode**. DUCK turns this voice down while the source sounds, FILTER opens this voice's filter with the source's level, and RING multiplies the two. **Amount** sets how much. **Choke** puts the voice in a group from 1 to 4. A hit on any voice in the group fades the others out within 1.5 ms.

Drum 1–3 also have **models** (EDIT, after the transient: MODEL), ported from Mutable Instruments **Rings** (MIT licence) to fixed point: **MODAL** (Rings' modal resonator, up to 32 modes: bars, plates, bells, drum heads), **STRING** (its string, with dispersion and a curved bridge) and **FM** (its two-operator FM voice). The knobs are Rings': **decay** (DAMPING), **bright** (BRIGHTNESS), **struct** (STRUCTURE), **position** (POSITION), plus **exciter** (0: Rings' own exciter at the hit; 127: the voice's own LXR sound, as if patched into Rings' input) and **tension** (the pitch drop of a hard hit). The voice's filter, shape and sends still apply. Rings' sympathetic strings and reverb aren't ported. `tests/rings_compare.cc` checks the port against Rings' own code. Projects from 0.8 and before load their old models (SKIN, BAR, BELL, PLATE, TUBE) as MODAL or STRING.

From 0.15 the MODEL list also has Plaits' sixteen original engines (**P VA** … **P HH**: virtual analog, waveshaping, FM, grain, additive, wavetable, chords, speech, swarm, filtered and particle noise, inharmonic string, modal, bass drum, snare, hi-hat), ported from Mutable Instruments **Plaits** (MIT licence). Their knobs are Plaits': **Harmonic**, **Timbre**, **Morph**, plus **Colour** (Plaits' low-pass gate on the amp EG; 0 a plain VCA), **Aux** (OUT to AUX) and **Env** (the amp EG's amount on Timbre and Morph). `tests/plaits_compare.sh` checks them against Plaits' own code. The models are **experimental** (not part of the LXR-02) and heavy: at most two play at once, rendered with D1–D3 on the FM-1's second core (one on a unit that falls back to one core; CONFIG → LOAD shows the cores and the load by part).

**Control All** (as on Elektron's boxes): hold OCT+ on a sound page and turn a knob, and that parameter moves on every track that has it, each from its own value, so the kit keeps its balance. Keep holding and page (SELECT, the sound buttons) to move more. Letting go of OCT+ asks whether to revert: OCT+ again reverts the kit (the default), OCT− keeps the changes, so a slipped thumb never keeps them by accident. It records no locks.

**KEYS** (SHIFT + EDIT): the white and black keys play the selected track's voice chromatically (KNOB 1: octave). Under REC the notes go into the steps. The keys have no velocity, so KNOB 2 sets it (for rolls too) and KNOB 3 the accent, played with SHIFT.

**Sound browser:** on a track's sound pages, turn PRESETS to step through the sounds that track can take (the library's sounds, then the voices of the library's kits, the kit slots and the factory bank), each auditioned; OCT+ keeps one, OCT− goes back. SAVE → SOUND saves a track's sound to the library.

The FX bus has two new types. **Phaser**: rate, depth, feedback, centre. **Flanger**: rate, depth, feedback (negative below the middle) and delay. Both sound strongest at DRY/WET 50%.

## MIDI and sync

- **In:** USB MIDI and the TRS jack. Notes on each track's channel play its voice; the global channel plays the selected track. CC *n* sets parameter *n*−1 and NRPN reaches the rest, as on the LXR. CC0 picks a kit, program change 0–31 a pattern.
- **Elixir's own parameters** (NRPN, six numbers per voice-based setting, in the order D1 D2 D3 SN CY HH): 174–179 FX send, 180–185 reverb send, 186 reverb size, 187 damping, 188 level, 189 model (0 room, 1 spring, 2 hall), 190–195 bits, 196–201 waveshaper, 202–207 tilt, 208–213 choke group, 214–219 random pitch, 220–225 random decay, 226–231 random filter, 232–237 random pan, 238–243 cross-mod source (0 none, 1–6), 244–249 cross-mod mode (0 duck, 1 filter, 2 ring), 250–255 cross-mod amount.
- **Remote map** (CONFIG → REMOTE, channel 16 by default): rows of eight for a controller such as the Launch Control XL, columns 1–7 the tracks and 8 the master. Knobs on CCs 8–119 (volume, pan, filter, resonance, decay, pitch, drive, sends, sample rate, LFO, random, macros, plus master tempo, morph, DJ filter, FX and reverb), buttons on notes 36–93 (mute, solo, select, rolls, DRILL, PERF's effects held or latched, PLAY, REC, patterns). Knobs PICKUP, ABS or REL. Values and button states go back out over USB. Under REC a knob records locks on its own track. The table is in the manual (§15).
- **Out:** USB MIDI. The FM-1 has no MIDI out jack.
- **Clock out:** always sent, 24 PPQ, plus start/stop.
- **Clock in:** turn the tempo below its minimum to **MIDI**. Elixir then follows incoming clock and start/continue/stop.

## Try it in the browser

```sh
tools/gen.sh           # generated headers (needs Python 3 with Pillow and fontTools)
web/emu/build.sh       # needs Emscripten -> build/emu/
cd build/emu && python3 -m http.server 8765    # open http://localhost:8765
```

The emulator runs the firmware's own C sources compiled to WebAssembly, on a simulated FM-1. Use the computer keyboard or the mouse. Enable Web MIDI to play it from a controller and to send its clock to other apps.

## Library: backup, restore, LXR-02 kits

`web/library.html` (the site's `library/`) talks to Elixir over USB MIDI SysEx (`firmware/src/library.c`, protocol in its header; `web/library.js`). It backs up and restores projects, imports LXR-02 .SND kits, imports a whole LXR-02 project folder (kits, patterns in V1–V3 formats, songs; Elixir's 32 patterns take one half of the LXR's 64), and backs up and restores the kit library (`.ktlb`); the emulator page has the same under **Library**.

**Kit library** (`firmware/src/kitlib.c`): 128 kits and 128 single sounds in flash at 0xEA000–0xFBFFF, outside the projects, read one at a time (no RAM per kit). PRESETS' kit browser shows it with KNOB 1 at From LIBRARY, SAVE / LOAD → KIT and MORPH reach it with KNOB 3 (To / From LIBRARY), SAVE / LOAD → SOUND saves or loads one track's sound, and the sound browser lists it. Writes are a log with a commit record written last, so a write cut short leaves the slot as it was. That flash area is outside Elixir's updater and the stock firmware's known store; that the stock V15 leaves it alone is not yet confirmed on a unit, so back the library up before installing other firmware. The library page and the emulator's Library import LXR-02 kits into it or into the slots (a folder: its subfolders too); in the emulator, kits can be tried first and the library downloaded for the FM-1.

The LXR-02's factory and Hrtl kits can't be redistributed, but a private build can include your own copies: `ELIXIR_KITS_DIR=dir:dir ./build.sh` (and `web/emu/build.sh`) generates `build/gen/factory_kits.h` (`tools/gen_factory_kits.py`). The version gets "+K", a fresh unit starts with those kits, LOAD has a FACTORY type, and `web/make_site.py` refuses to publish the build.

## Build the firmware

Same requirements as Felucca: the JieLi pi32v2 toolchain (`~/.jieli/toolchain`), the JieLi AC79 SDK (`~/fw-AC79_AIoT_SDK`), Python 3 with Pillow and fontTools, and Docker on macOS (the toolchain is Linux x86-64).

```sh
PYTHON=/path/to/python ./build.sh       # -> build/elixir.fwsc
```

## Install

`web/make_site.py` makes Elixir's site in `build/site` (after `./build.sh` and `web/emu/build.sh`): the front page (`web/index.html`, `web/site.css`; its screens in `web/shots/`, made from ui_test's by `tools/site_shots.py`), the install page at `install/` (Web MIDI, Chrome or Edge: a device check, official V15 first, Elixir, and back to V15), the emulator (`try/`), the manual as a page (`manual/`), and the package, source and licences (`firmware/`). Any static host serves it. For GitHub Pages, `tools/publish_site.sh` builds it and commits it to the `gh-pages` branch (then `git push origin gh-pages`; Settings → Pages → Deploy from a branch: `gh-pages`, root). The source is at https://github.com/dmajka/elixir (`REPO` in `web/make_site.py`; the pages link it). Install the official V15 first (see the manual's section 1).

Or install `build/elixir.fwsc` over USB:

```sh
python3 tools/fm1_install.py build/elixir.fwsc
```

To go back, install the official firmware file (`fm1_install.py FM-1.fwsc`) or any other alternative firmware. Elixir stores its projects and settings in the same flash region Felucca uses for its own data, so installing Elixir replaces data saved by Felucca.

Projects saved by older versions load in newer ones. Elixir 0.1 had 64 patterns; newer versions keep the first 32, and songs that pointed past them play pattern 32 instead. Projects do not load in older versions than the one that saved them.

## Tests

```sh
cc -O2 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal tests/render_test.c -o build/render_test && build/render_test build/
cc -O1 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal tests/ui_test.c -o build/ui_test -lm && build/ui_test build/shots
```

```sh
cc -O1 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal tests/library_bridge.c -o build/library_bridge
node tests/library_test.mjs [LXR project folder]   # the library page's code against the device's
node tests/install_test.mjs                          # the install page's updater against simulated FM-1s
```

```sh
EURORACK=path/to/eurorack STMLIB=path/to/stmlib tests/rings_compare.sh   # the models against Rings' own code
```

`render_test` plays every voice and counts the MIDI clock. `ui_test` drives the panel as a user would (programming a beat, sound pages, locks, patterns, songs, saving and loading a project, MIDI sync in) and writes a screenshot of each screen to `build/shots/`. It also checks the sends, the reverb, the sound shaping, the phaser and flanger, loads a Elixir 0.1 project (`tests/data/proj_v1.bin`), and prints how big a busy and a dense project get. `compare_test` checks the voices against the original LXR DSP sources and needs the LXR repository (see its header). `library_test` runs the library page's own code against the device's through `library_bridge`: kits in and out, backups, restores, LXR projects (the LXR kits from `../lxr-02_kits` if there, or the project folder given), and the kit library. `install_test` runs the install page's updater (`web/fm1ota.js`) and device check (`web/install.js`) with the built package against simulated FM-1s over a mock Web MIDI: Kettle to Elixir (also when the computer keeps the name Kettle, a Windows-style port name, a release build's identity, a write cut off and resumed), Elixir to Elixir and V15 to Elixir, other MIDI gear left alone. `rings_compare` renders the models with Elixir's fixed-point port and with Rings' float code (github.com/pichenettes/eurorack and stmlib, not in this tree; the script sets their sample rate to Elixir's 44.1 kHz in a copy) over pitches and settings, and fails below a correlation of 0.99 or above an error of −30 dB.

## Licence

GPL-3.0-only. See [NOTICE.md](NOTICE.md) and `LICENSES/`.
