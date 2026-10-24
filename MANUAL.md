# Kettle manual

Kettle is the firmware of the Erica Synths LXR-02 drum machine, ported to the M-VAVE FM-1 and extended. This manual covers version 0.6: the LXR's sound engine and sequencer as they map onto the FM-1's keys, buttons and knobs, and everything Kettle adds.

Kettle is **unofficial**. It is not affiliated with or endorsed by Erica Synths, M-VAVE or Hügelton Instruments. It is free software under the GPL-3.0 (see *Licence and credits* at the end).

---

## Contents

1. [Before you install](#1-before-you-install)
2. [Quick start: a beat in two minutes](#2-quick-start-a-beat-in-two-minutes)
3. [The panel](#3-the-panel)
4. [Tracks and voices](#4-tracks-and-voices)
5. [Programming patterns](#5-programming-patterns)
6. [Step edit: velocity, timing, locks, conditions, ratchets](#6-step-edit)
7. [Recording live](#7-recording-live)
8. [Patterns, banks and songs](#8-patterns-banks-and-songs)
9. [Shaping the sound](#9-shaping-the-sound)
10. [Mixer, sends, FX bus and reverb](#10-mixer-sends-fx-bus-and-reverb)
11. [Kits and morph](#11-kits-and-morph)
12. [Generating tracks: Euclid, generators, Grids, track clocks](#12-generating-tracks)
13. [Performance mode (PERF)](#13-performance-mode-perf)
14. [Saving: kits, patterns, songs, projects](#14-saving)
15. [MIDI](#15-midi)
16. [Settings (CONFIG) and the CPU meter](#16-settings-config-and-the-cpu-meter)
17. [Reference tables](#17-reference-tables)
18. [How Kettle differs from the LXR-02](#18-how-kettle-differs-from-the-lxr-02)
19. [Troubleshooting](#19-troubleshooting)
20. [Licence and credits](#20-licence-and-credits)

---

## 1. Before you install

### Status

Kettle has been tested extensively in its browser emulator and with host tests (the same C sources compiled for a computer), and **it has run on one real FM-1 so far** (October 2026). There, a busy pattern used about 60% of the processor on average, with a 64% peak. Kettle shows its load on screen (see [the CPU meter](#16-settings-config-and-the-cpu-meter)); reports from other FM-1s are welcome. Version 0.6 (PERFORM 2 and the physical models) has so far only run in the emulator: its load on the FM-1 hasn't been measured yet, so watch the CPU meter with several models or PERFORM 2 effects at once.

### Before the first install: official V15

Put the FM-1 on M-VAVE's official firmware **V15** before installing Kettle for the first time. This is what everyone who makes FM-1 firmware recommends. Kettle's package, like Felucca's, is built for V15's flash layout and update loader, and V15 is the firmware you return to.

The install page has a **Check my FM-1** button. It asks the FM-1 what it runs: `FM-1_015` is official V15, `FM-1_9xx` is an alternative firmware (Felucca or Kettle), and anything else is an older official version. If it isn't V15, get V15 (`FM-1.fwsc`) from M-VAVE's download page and install it with the page's **Official V15** section, or with M-VAVE's own updater.

### Installing

Use the Kettle install page (`index.html` of the Kettle site) in Chrome or Edge. Connect the FM-1 straight to the computer with a USB data cable, close other apps that use MIDI, and press **Install Kettle**. Allow MIDI (SysEx) when the browser asks. The page sends the package, waits for the FM-1 to restart and checks that it now runs Kettle. Don't unplug the cable or close the page while it writes.

From a terminal, `tools/fm1_install.py` does the same. It needs Python 3 with `mido` and `python-rtmidi`:

```sh
python3 tools/fm1_install.py kettle.fwsc
```

### Going back

Install the official `FM-1.fwsc` (V15) with the install page's **Official V15** section, or with `fm1_install.py FM-1.fwsc`. Kettle has no backup of its own: copy anything you want to keep before you go back. (The page checks that the file is the unmodified V15, so a damaged or wrong file can't be written.)

Kettle keeps its projects and settings in the same flash area Felucca uses for its own data, so **installing Kettle replaces anything saved by Felucca** (back it up with Felucca's own page first), and installing something else afterwards replaces Kettle's projects.

### If something goes wrong

How an update works decides what can be recovered. The firmware that is running (official, Felucca or Kettle) answers the installer, checks the package and starts the FM-1's **update loader**. The loader writes the new firmware and restarts. So:

- **An install was cut off** (cable pulled, page closed, computer asleep): the FM-1 stays in update mode, waiting for the loader's write. Plug it in and press Install again (or run `fm1_install.py` again). It finishes the write. This works for Kettle and for V15.
- **Kettle runs but something is wrong** (it misbehaves, it's too slow, you don't like it): install V15 from the page. Kettle answers the installer like any other firmware.
- **Kettle crashes as it starts** (it never reaches the grid): this is the case to worry about, because no firmware is left to answer the installer. Kettle's boot guard notices two crashed starts in a row and sends the FM-1 to the chip's own **UBOOT** mode on the next start. UBOOT is built into the chip and survives any firmware, but neither the install page nor `fm1_install.py` can write from it. Recovery then needs Leo Kuroshita's [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter): a Seeed XIAO RP2040 board wired to the FM-1's USB lines, which can rewrite its flash.
- **Hold OCT− and OCT+ together for 5 seconds** to enter UBOOT by hand (the screen shows UBOOT). This is only useful with the Transporter. Turning the FM-1 off and on should leave it, but this hasn't been tried on hardware.

Kettle has installed and started on one real FM-1, from V15, with the install page. That is still very little testing: install it knowing that a start-up crash would need the Transporter to undo.

---

## 2. Quick start: a beat in two minutes

1. Press **HOME**. The screen shows the grid: seven tracks (D1 D2 D3 SN CY CH OH) by sixteen steps.
2. Press black key **1** (D1, the kick). Tap white keys **1, 5, 9, 13**. They light up: the kick plays on every beat.
3. Press black key **4** (SN). Tap white keys **5** and **13**.
4. Press black key **6** (CH, closed hat). Tap white keys **1, 3, 5, 7, 9, 11, 13, 15**.
5. Press **PLAY**.
6. Turn **KNOB 1** to change the tempo, **KNOB 2** for shuffle.
7. Select D1 again (black key 1), press **EDIT** and turn the knobs to change the kick's pitch and waveform. Press **ENV** for its envelope.
8. Hold COPY (**OCT+**) and press black key **9** (bar 2): bar 1 of every track is copied into bar 2 and the pattern is now two bars long. Press bar key **9** again on its own to see bar 2, and change it.
9. Press **SAVE**, turn KNOB 1 to PROJECT, press **OCT+**, name it, press **OCT+** again.

---

## 3. The panel

### Keys, buttons and knobs

The FM-1's keyboard has 16 white keys and 11 black keys, numbered left to right in this manual.

| Control | What it does in Kettle |
|---|---|
| **White keys 1–16** | The 16 steps of the bar on screen. On the pattern page: patterns. In PERF: rolls and effects. |
| **Black keys 1–7** | Select a track: D1 D2 D3 SN CY CH OH. SHIFT + key: mute / unmute it. |
| **Black keys 8–11** | Show bar 1–4. On pattern, song, save and load pages: the bank of 16. SHIFT + key: track length (16, 32, 48, 64). COPY + key: copy the shown bar into it. In PERF: the repeat length. |
| **OCT−** | **SHIFT** (hold). While held, the screen shows what SHIFT + each key does. |
| **OCT+** | **COPY** (hold). SHIFT + OCT+ = **CLEAR**. OK in the save, load and action dialogs. |
| **EDIT** | Sound: oscillator → FM → transient (press again to step through) |
| **ENV** | Sound: amp envelope → mod envelope / velocity |
| **SCL** | Sound: filter → shape |
| **LFO** | Sound: LFO → random per hit → cross mod / choke |
| **GLO** | Sound: mix → mixer → config |
| **FX** | FX bus → reverb (turn SELECT for the pages) |
| **SEQ** | Step edit (press again to go back) |
| **ARP** | PERF → Euclid / generator / track clock / Grids map |
| **HOME** | Grid → patterns → song |
| **SAVE** | Save → load (SHIFT + SAVE: load) |
| **PLAY** | Start / stop. SHIFT + PLAY: play the song from its start. |
| **REC** | Record on / off. SHIFT + REC: record options. |
| **ALGORITHM** (encoder) | Select the track |
| **SELECT** (encoder) | The page; in lists, the row |
| **PRESETS** (encoder) | The kit browser |
| **KNOB 1–4** | The four parameters shown at the bottom of the screen |
| **MASTER** (pot) | Output volume |

**Locks:** on any sound page, *hold a step key and turn a knob* to set that parameter for that step only (a parameter lock). See [locks](#locks).

### The screen

| Area | Shows |
|---|---|
| Header (top line) | The playing pattern (P01), the queued one (>02), the kit's name or SONG nn, M when morph is on, MIDI when synced to MIDI clock, the tempo, play (triangle, bright on the beat) and record (red dot) |
| Title | The selected track (coloured tag), the page's name, page dots (or "3/14" when there are many pages), or a message |
| Body | The grid, or the page's own display |
| Cards (bottom) | The four knobs: each parameter's name, value and position. The card of the last knob turned is highlighted. A locked value shows in the accent colour. |

### LEDs

- **Step keys:** lit = step on; the playing step flips its light; beats (1, 5, 9, 13) glow faintly.
- **Track keys:** the selected track lit; each flashes when its track plays; a muted track goes dark.
- **Bar keys:** the shown bar lit, the playing bar breathing, bars past the track's length dark.
- **Buttons:** the current mode's button lit; REC lit when recording; OCT− lit while SHIFT is held, OCT+ while COPY is held; OCT+ breathes when it will act as OK.
- **PLAY:** green while playing, dark for half a step on each beat.

---

## 4. Tracks and voices

Kettle has the LXR-02's six voices on seven tracks:

| Track | Key | Voice |
|---|---|---|
| D1, D2, D3 | black 1–3 | Drum voices: oscillator plus FM or mix oscillator, pitch envelope, transient |
| SN | black 4 | Snare / clap: oscillator plus filtered noise, repeating envelope (claps) |
| CY | black 5 | Cymbal: three-operator FM |
| CH | black 6 | Closed hi-hat: three-operator FM |
| OH | black 7 | Open hi-hat: the same voice as CH with its open decay; one cuts the other |

Every voice has a transient generator (click), an amp envelope, a filter, a waveshaper, a sample-rate reducer, pan and an output, plus Kettle's additions: sends, bit reduction, tilt EQ, waveshaper choice, random per hit, choke groups and cross modulation.

---

## 5. Programming patterns

A pattern has seven tracks of up to 64 steps (four bars of 16). Each track has its own length, so tracks can loop against each other (polymeter).

### The grid (HOME)

- **Select a track** with its black key (or ALGORITHM).
- **Tap a white key** to turn that step on or off.
- **Bar keys** (black 8–11) show bars 1–4. With FOLLOW on (CONFIG), the screen follows the playhead.
- **Knobs:** tempo, shuffle, the track's length, the track's volume.

On the grid, a step's height shows its velocity, an outline means a chance below 100%, a notch means a flam, a tick underneath means a shift, a dot top right means a lock, a dot top left means a trig condition, and a bar underneath means a ratchet.

### Track length

- **SHIFT + bar key:** the selected track becomes 1, 2, 3 or 4 bars long.
- **Length knob** on the grid (KNOB 3): any length from 1 to 64.
- **LAST** (SHIFT + white key 14): tap the step that should be the track's last. Press SHIFT + 14 again to set all tracks at once.

The pattern lasts as long as its longest track (in time, counting [track clocks](#track-clock)). Shorter tracks keep looping on their own across the pattern's end.

### Copying

- **Copy a bar to another bar:** on the grid, hold **COPY** (OCT+) and press a bar key. The shown bar is copied, *every track*, into that bar, with each step's velocity, locks, timing, conditions and ratchets. A track that ended before the target bar grows to include it, unless its length isn't a whole number of bars (a 12-step track stays at 12). On other pages (sound pages, step edit), COPY + bar key copies the selected track only.
- **Copy a track:** hold COPY, press the source track's black key, then the target's.
- **Copy a pattern:** on the pattern page, hold COPY, press the source pattern's white key, then the target's.

### Clearing

Hold SHIFT and press **OCT+** (CLEAR). A list opens; turn SELECT to choose, then press SHIFT + OCT+ again to confirm. Letting go of SHIFT cancels.

| Choice | Clears |
|---|---|
| CLEAR TRACK | the selected track's steps |
| CLEAR PATTERN | every track of the pattern |
| CLEAR AUTOMATION 1 / 2 | lock lane 1 or 2 of the selected track |
| CLEAR KIT (INIT) | the sound back to the init kit |
| DELETE SONG ROW | (song page) the selected row |

### Mutes

SHIFT + a track's black key mutes or unmutes it (in PERF, the black keys mute on their own).

### Tempo, shuffle, MIDI sync

- **Tempo:** KNOB 1 on the grid, the PERF page or CONFIG. Turn it all the way down to **MIDI** to follow incoming MIDI clock.
- **Shuffle:** delays every other 16th; saved with the pattern.

---

## 6. Step edit

Press **SEQ**. Tap a white key to select a step (its key breathes; tap it again to turn the step on or off). Turn **SELECT** for the four pages:

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
|---|---|---|---|---|
| STEP | Velocity | Note | Chance | Condition |
| TIMING | Flam | Shift | – | – |
| LOCKS | Lock 1 parameter | its value | Lock 2 parameter | its value |
| RATCHET | Hits | Rate (bouncing: Curve) | Fade | Shape |

- **Note:** the pitch the voice plays at (C4 = as set).
- **Chance:** the probability the step plays (PERF's FILL key plays it anyway).
- **Flam:** OFF, x2, x3, x4, x6, x8: the step repeats evenly inside its length.
- **Shift:** −7 … +7 eighths of a step, early or late.
- **Ratchet:** the step plays Hits times, evenly at Rate (1/32, 1/48, 1/64, 1/24). Fade: FLAT, DOWN or UP.

### Bouncing ratchets

Turn **Shape** to **BOUNCE** and the ratchet falls like a dropped ball: the hits start far apart and come closer and closer together. With BOUNCE, KNOB 1 sets 3, 4, 5, 6, 8, 12 or 16 hits, and KNOB 2 sets the **Curve**:

| Curve | Each gap |
|---|---|
| **BALL** | 0.7 of the one before |
| **TIGHT** | 0.55 of the one before: a quick rattle |
| **SLOW** | 1.3 of the one before: it spreads out and slows down (half a bar at most) |
| **DRUNK** | 0.7, give or take 15% each time |

The first gap and a pitch drift are set for the whole track on the TRACK CLOCK page (section 12): **Bounce** (1/4, 1/8, 1/16, 1/32; 1/8 at first) and **Bnc pitch** (−12 … +12 semitones over the bounce: −5 makes a tom fall as it settles). Fade works on bounces too.

A bounce plays out over the steps that follow it. The next hit on the same track cuts it off, so leave room after it. TAPE STOP slows bounces with everything else.

MIDI notes played while in step edit set the selected step's note.

### Locks

A lock sets one parameter to a different value on one step, for example a higher filter cutoff on the last kick of the bar. Each step can hold **two** locks.

- **The quick way:** on any sound page (EDIT, ENV, SCL, LFO, GLO, the mixer), hold a step key and turn a knob. The step turns on if it was off, and the knob's card shows the locked value in the accent colour.
- **On the LOCKS page:** choose any lockable parameter (any voice's sound parameter, the FX bus, the sends, the reverb, Kettle's shaping parameters) and its value.

Every sound parameter can be locked except the reverb's model.

### Trig conditions

The Condition knob (STEP page) sets *when* a step plays:

| Condition | Plays |
|---|---|
| – | always (subject to its chance) |
| A:B | the A-th time of every B times round the track (1:2, 2:2, 1:3 … 8:8) |
| FILL / !FILL | only while PERF's FILL key is held / only while it isn't |
| PRE / !PRE | if this track's last conditional step played / didn't |
| NEI / !NEI | if the previous track's last conditional step played / didn't |
| 1ST / !1ST | only the first time round after PLAY / every time but the first |

"Times round" are counted per track from PLAY, so on a 12-step track, 1:2 plays every other 12 steps. The step's chance still applies on top of its condition.

### Ratchets

A ratchet repeats the step: **Hits** 2–8, at a **Rate** of 1/32, 1/48, 1/64 or 1/24, with the velocity **FLAT**, fading **DOWN** or rising **UP**. Unlike a flam (which divides the step), a ratchet keeps its own rate and can run past its step.

---

## 7. Recording live

Press **REC** (it lights), then **PLAY**.

- **Notes** you play (MIDI notes on a track's channel, or rolls in PERF) are written into the nearest step.
- **Knob moves** on sound pages are written as locks onto the step that is playing, if that step is on.
- **SHIFT + CLEAR while recording** erases the selected track's steps as they pass.
- **SHIFT + REC** opens the record options: the lock lane (1 or 2) knob moves record into.

---

## 8. Patterns, banks and songs

Kettle has **32 patterns** in two banks of 16, and **64 songs**.

### The pattern page (HOME, press again)

- **White key:** queue that pattern. While playing, it starts when the current pattern ends (the header shows >nn); while stopped, at once.
- **Bar keys 8–9:** bank 1 (patterns 1–16) or bank 2 (17–32).
- **Knobs:** kit change (AUTO: switching to a pattern also loads the kit saved with it), shuffle.

**Save before you switch.** As on the LXR, changes to the playing pattern that haven't been saved (SAVE → PATTERN) are lost when another pattern starts. **RELOAD** (SHIFT + white key 13) brings back the kit and pattern as saved. Saving a project also keeps the playing pattern's changes.

### Songs (HOME, press a third time)

A song is up to 64 rows, each a pattern (or END / LOOP).

- **SELECT:** the row.
- **White key:** write that pattern into the row and move to the next (bar keys pick the bank).
- **KNOB 1:** the row's pattern, END or LOOP; **KNOB 2:** which song.
- **COPY:** insert a row (a copy of the one above). **CLEAR → DELETE SONG ROW:** remove it.
- **SHIFT + PLAY:** play the song from the start. PLAY on its own plays the pattern alone.

---

## 9. Shaping the sound

Select a track, then press a sound button. Pressing the same button again moves to the next section; turning SELECT steps through every page. Each page shows eight parameters (two rows of four); the lit row is on the knobs, and pressing the button again (or SELECT) gives the other row.

### The LXR's sound pages

| Section (button) | Drum 1–3 | Snare | Cymbal, hats |
|---|---|---|---|
| **Oscillator** (EDIT) | Coarse, Fine, Waveform, Pulse width | Coarse, Fine, Noise frequency, Mix / Waveform, Pulse width | Coarse, Fine, Waveform, Pulse width (OH: no pulse width) |
| **FM** (EDIT) | Amount, Frequency, Waveform, Mode (FM or mix) | – | Mod 1 & 2 frequency and gain / their waveforms |
| **Transient** (EDIT) | Waveform, Volume, Frequency | same | same |
| **Model** (EDIT, Kettle's) | Model, Decay, Damping, Material / Position, Exciter, Tension (see *Physical models* below) | – | – |
| **Amp EG** (ENV) | Attack, Decay, Slope | Attack, Decay, Repeat count (claps), Slope | Attack, Decay (CH: closed; OH: open), Repeat (cymbal), Slope |
| **Mod EG / velocity** (ENV) | Pitch EG: Decay, Slope, Amount / Velocity: destination, amount, volume mod | same | Velocity: destination, amount, volume mod |
| **Filter** (SCL) | Frequency, Resonance, Type (LP HP BP UNITY BP NOTCH PEAK LP2 OFF), Overdrive | same | same |
| **LFO** (LFO) | Frequency, Clock sync, Amount, Waveform / Retrigger, Offset, Destination voice, Destination parameter | same | same |
| **Mix** (GLO) | Volume, Pan, Sample rate, Drive / Output, Length, MIDI channel, MIDI note | same | same |

The LXR's waveforms are SINE, TRI, SAW, SQUARE, NOISE and PWM; its transients SNAP, OFFSET, CLICK, CLICK 2, TICK, KICK, RIM, DRIP, HAT, CLAP, KICK 2, SNARE, TOM and SNAP 2. Each voice has one LFO, which can modulate any parameter of any voice; the velocity destination can be any of the voice's own parameters.

### Kettle's sound pages

**Shape** (SCL, after the filter):

| KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
|---|---|---|---|
| Shape | Drive | Bits | Tilt |

- **Shape** chooses the waveshaper the voice's **Drive** goes into: **LXR** (the original curve; the default), **FOLD** (folds back from full scale), **CLIP** (hard clip), **ASYM** (soft on top, clipped below: even harmonics), **RECTIFY** (full-wave: an octave up). For all but LXR, the drive sets a gain of 1× to 8×.
- **Bits:** OFF, then 15 down to 1. Works after the sample-rate reducer (Mix page) for a full bitcrusher.
- **Tilt:** a one-knob EQ around 700 Hz. 0 is flat, + brighter, − darker.

**Random per hit** (LFO, after the LFO's two pages):

| KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
|---|---|---|---|
| Pitch | Decay | Filter | Pan |

Each hit varies by up to the amount set: pitch up to an octave either way at 127 (small amounts are subtle, for humanising), decay from half to double, filter cutoff up to half its range either way, pan across the field.

**Cross mod / choke** (LFO, next):

| KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
|---|---|---|---|
| Source | Mode | Amount | Choke |

- **Source:** another voice that modulates this one.
- **Mode:** **DUCK** (this voice drops while the source sounds: sidechain), **FILTER** (the source's level opens this voice's filter), **RING** (the two multiplied).
- **Choke:** a group 1–4. A hit on any voice in the group fades the others in that group out (within 1.5 ms). The hats already choke each other, since they share a voice.

### Physical models (Drum 1–3)

The three DRUM voices have a second engine: instead of the LXR's oscillators, a struck body that rings. It is set in the **MODEL** section (EDIT, after the transient; drum tracks only):

| | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
|---|---|---|---|---|
| Row 1 | Model | Decay | Damping | Material |
| Row 2 | Position | Exciter | Tension | |

- **Model:** **LXR** (the LXR's own engine; the default, and what old kits and projects load as), **SKIN** (a drum head: six modes at 1, 1.59, 2.14, 2.30, 2.65, 2.92 times the pitch), **BAR** (marimba, xylophone: 1, 2.76, 5.40, 8.93 …), **BELL** (a hum an octave below, inharmonic, long), **PLATE** (dense and metallic, long) and **TUBE** (a plucked string or blown pipe).
- **Pitch** is the voice's own Coarse and Fine on the oscillator page; the pitch EG and random pitch still bend it.
- **Decay:** how long it rings, from 20 ms to about 4 s (64 is about 0.3 s). BELL and PLATE ring twice as long.
- **Damping:** how much faster the higher modes die. 0: they all ring as long as the lowest; high: a dull thud after the first instant.
- **Material:** stretches the mode ratios. Below 64 they draw together (softer, wooden); above, they spread (glassy, metallic).
- **Position:** where it's struck, from the edge (0) to the centre (127). Each mode is loudest where it moves most, so moving the strike point brings modes in and out; near the edge the fundamental is quieter.
- **Exciter:** what strikes it. 0 to 63: a pulse from a soft thump (0) to a hard click (63). 64 to 127: a burst of noise, longer as it rises (a brush or a scrape). A soft thump brings out the lower modes, a click all of them. The level stays about the same whatever the exciter, pitch or position.
- **Tension:** a hard hit stretches a drum head, so the pitch starts high and falls back in about 50 ms. Up to an octave at 127, scaled by velocity.

**Velocity** sets how hard it's struck: louder, and brighter, since a harder hit brings out the higher modes.

**TUBE** is a string or pipe (a Karplus-Strong loop): Decay is how long it sustains, Damping how dark it gets as it rings. Exciter sets the pluck: below 64 a pulse, from soft and round (0) to sharp (63); from 64 a noise burst, dark at 64 and bright at 127. It ignores Material and Position.

A new hit damps what is still ringing by half, as a mallet does, so fast repeats on a long decay don't pile up. Loud hits bend softly into full scale instead of clipping.

The model replaces the oscillators, transient and amp EG: the model's own Decay sets how long the voice sounds. Everything after the oscillator still applies: the filter, drive and shape, bits, tilt, sends, LFO, random per hit and cross modulation. The model's settings are part of the kit and can be locked per step; Model itself doesn't morph: the voice keeps its own kit's engine while the other knobs morph.

---

## 10. Mixer, sends, FX bus and reverb

### The mixer (GLO, after the Mix pages)

Turn SELECT for eight pages:

| Pages | Knobs |
|---|---|
| VOLUME (2 pages) | each voice's volume; FX dry/wet; the master level (shown, set by the MASTER pot) |
| PAN (2 pages) | each voice's pan; the reverb's level and size |
| FX SEND (2 pages) | each voice's FX send; FX dry/wet |
| REVERB SEND (2 pages) | each voice's reverb send; the reverb's level |

The bars show the current page's values, with each voice's level on the volume pages.

### Sends

- **FX send** sets how much of the voice goes *through* the FX bus instead of straight to its output. At 127 all of it does. (This is the LXR's "Out = FX BUS"; kits from the LXR or from older Kettle versions that used it load as FX send 127.)
- **Reverb send** feeds the voice into the reverb, on top of its output.

### The FX bus (FX)

One effect at a time.

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
|---|---|---|---|---|
| FX BUS | Type | Dry/Wet | Output | the type's mode |
| (parameters) | P1 | P2 | P3 | P4 |
| REVERB | Model | Size | Damping | Level |

| Type | Mode | P1 | P2 | P3 | P4 |
|---|---|---|---|---|---|
| DRIVE | TUBE / FOLD / CLIP | Drive | Color | Tone | Volume |
| RING | the carrier waveform | Frequency | – | – | – |
| COMP | ratio 1:1 … 8:1 | Attack | Release | Threshold | Gain |
| DELAY | MONO / PING PONG | Time | Range | Tone | Feedback |
| PHASER | – | Rate | Depth | Feedback | Centre |
| FLANGER | – | Rate | Depth | Feedback (below the middle: negative) | Delay |

The phaser and flanger sound strongest at DRY/WET 50%. Output sends the bus to stereo or one side.

### Reverb

A separate send bus after the FX bus, from Felucca: **ROOM** (a Freeverb-style room), **SPRING** (one spring of a spring tank: chirps and drips), **HALL** (a dense, smooth, stereo hall). Size sets the length, Damping the darkness, Level the return. When nothing is fed to it and its tail has died away, it stops processing to save CPU.

---

## 11. Kits and morph

A kit is every sound parameter of all six voices (including the sends, the reverb and Kettle's shaping). Kettle holds **64 kits**.

- **Browse and load:** turn **PRESETS**. The kit list opens; turn to choose, press **OCT+** to load. It closes by itself after 6 seconds.
- **Load / save:** SHIFT + white key 8 opens LOAD KIT; SAVE opens SAVE KIT (see [saving](#14-saving)).
- **LXR-02 kits:** kits from an LXR-02 SD card (.SND files) come in through the [library page](#backing-up-restoring-and-lxr-02-kits). A voice the LXR routed to its FX bus comes in with FX SEND 127, which sounds the same. Kettle's own settings (sends, reverb, shaping, models) start at their defaults.
- **Morph:** load a second kit as the *morph target* (SHIFT + white key 9: LOAD MORPH), then turn **Morph** (PERF's first page, KNOB 4) from 0 to 100%. Every sound parameter crossfades towards the target (outputs, modulation targets and the reverb model stay). The header shows M while morph is above 0. SAVE → MORPH saves the mixture you hear as a new kit.

---

## 12. Generating tracks

Press **ARP** twice (past PERF). Turn SELECT for six pages. The first four are for the selected track; the last two (the links) are for the pattern:

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
|---|---|---|---|---|
| EUCLID | Length | Hits | Rotate | Action |
| GENERATOR | Generator | its settings … | | |
| TRACK CLOCK | Length | Clock | Bounce | Bnc pitch |
| GRIDS MAP | Map X | Map Y | Chaos | – |
| LINKS | Link (1–8) | Start | Amount | Depth |
| LINK SETUP | When | Target | Effect | Window |

### Euclid

Turning Length, Hits or Rotate rewrites the track at once: that many hits spread as evenly as possible over that length, rotated. Each track keeps its own Euclid settings.

### Actions

Turn **Action** (EUCLID page, KNOB 4), then press **OCT+** to do it:

| Action | Does |
|---|---|
| **DICE** | A new track at random: the Euclid page's number of hits, anywhere over its length |
| **COMMIT** | Writes the generator's live track into the pattern |
| **UNDO** | Takes back the last DICE, COMMIT or Euclid change on this track. UNDO again redoes it. |
| **RESET** | Starts the generator's live track again from the pattern |

### Generators

A generator plays a **live copy** of the track and changes it as it plays; the grid shows what is playing. The pattern itself isn't changed until you COMMIT. Turning a generator on, RESET, or changing pattern starts the live copy again from the pattern; stopping and starting doesn't.

| Generator | Settings | What it does |
|---|---|---|
| **MUTATE** | Amount, –, Freeze | Each time round, up to two changes at the Amount's chance: a step flipped, a step moved to a free neighbour, a lock's (or the velocity's) value drifted. Freeze stops it. |
| **TURING** | Length, Lock, Density | A shift register (2–16 bits), as in a Turing Machine module. Lock 127: the loop repeats exactly; lower, its bits flip at random more often. Density: how many steps play. The velocity follows the register. |
| **CELLULAR** | Rule, –, Freeze | Each time round the steps become the next row of a cellular automaton (rule 0–255, Wolfram's numbering: 30, 90, 110 …). An empty row starts again from the pattern. |
| **GRIDS** | Part, Density, Accent | Emilie Gillet's Grids drum map: see below. |

Steps you tap on a generator's track change both the pattern and the live copy.

### Grids

GRIDS plays a track from Grids' map of drum patterns. Each Grids track takes a **part** (BD, SD or HH; it starts as BD for D1–D3, SD for the snare, HH for the cymbal and hats), a **density** (how many of the map's hits play) and **accent** (the velocity follows the map).

The map's **X**, **Y** and **Chaos** are on the GRIDS MAP page and belong to the pattern, as on the module: one position drives every Grids track, so moving X or Y changes the kick, snare and hats together. Chaos varies the hits a little each time round.

A typical start: D1 GRIDS / BD, SN GRIDS / SD, CH GRIDS / HH, then explore X and Y, and COMMIT what you like.

### Track clock

Each track can run at its own rate against the pattern: **/4, /3, /2, 2/3, 3/4, 1x, 4/3, 3/2, x2, x3, x4**. A hat at x2 plays 32nds; a track at 3/4 against a 16-step pattern makes 12 steps fit a bar (a polyrhythm). Combine with track lengths for polymeters. Flams and shifts scale with the track's step.

**Bounce** and **Bnc pitch** set the track's bouncing ratchets (section 6): the first gap and the pitch drift.

### Links (track coupling)

Links make tracks react to each other, so a pattern behaves like a small system instead of a loop: the kick ducks the hats, the open hat answers only when the closed hat rests, a snare hit makes the next hat rattle. Each pattern holds up to **8 links**. **SHIFT + SEQ** opens the LINKS page from anywhere (it is also the fifth EUCLID page).

A link reads as a sentence, and the screen shows it that way:

> When **D1** hits, **CH** chance −60%, on the next step.

| Part | Choices |
|---|---|
| **When** (KNOB 1 on LINK SETUP) | A track, and what it does: **hit**, **acc** (hits hard: velocity above 100), or **rest** (its step had nothing, its chance failed, it's muted, or a link dropped its hit) |
| **Target** | The track that reacts |
| **Effect** | **DUCK** (its hit becomes less likely by the amount), **ONLY** (it plays only in the window, otherwise it's dropped with the amount's chance), **TRIG** (an extra hit at the amount's velocity), **RATCH** (its hit ratchets +1 to +4 at 1/32, or BOUNCE), **VEL** (velocity ±), **PITCH** (± semitones), **SHIFT** (plays late, up to half a step at 100%), **DECAY** (its decay ±, for that hit) |
| **Window** | Which of the target's steps react: **SAME** step, **NEXT** step, **NEXT 2** steps, or the rest of the **BAR**. For TRIG, the window is a delay: at once, the next step, two steps on, the next bar. |
| **Amount** (LINKS page) | How much, in the effect's own units |
| **Depth** (LINKS page) | One amount for the pattern that scales every link: 0% turns them all off, so you can fade the coupling in live |

On the LINKS page, KNOB 1 picks the link (the row of 8 at the top shows which are set) and **Start** offers four starting points, so a useful link is one turn away:

| Start | The link |
|---|---|
| **DUCK HATS** | When D1 hits, CH chance −60%, on the next step |
| **CALL / RESPONSE** | When CH rests, OH plays only then, on the next 2 steps |
| **GHOST FILL** | When D1 rests, D2 plays at 30, at once |
| **CHAIN** | When SN hits, CH ratchets +2, on the next step |

Start shows EDITED once you change the link; turning it back to – clears the link. On the grid, a small mark by a track's name shows that a link joins it.

**How they work.** On each tick, Kettle first decides every track's hit (steps, chance, conditions, generators). Then it applies the links in order, 1 to 8, each one seeing what the links before it did. Then the hits play. So a link can point either way between two tracks, and loops are fine: if link 1 has D1 duck D2 and link 2 has D2 duck D1, link 1 wins. A flam's or ratchet's later hits keep what the links decided for its first hit, and the extra hits a link makes (TRIG) don't set off other links.

**A worked example: a hat line that breathes.**
1. Program four kicks on D1 (steps 1, 5, 9, 13) and closed hats on every step of CH.
2. SHIFT + SEQ, then turn **Start** to DUCK. The hat after each kick now drops out 60% of the time.
3. KNOB 1 to link 2, Start to CALL. Program open hats on every step of OH: they now play only in the two steps after a closed hat rests, so they answer the gaps DUCK made. Every bar comes out a little different.
4. KNOB 1 to link 3, then on LINK SETUP set When to **SN hit**, Target **CH**, Effect **RATCH**, Window **NEXT**. Add a snare on 5 and 13: the hat after it rattles.
5. Back on LINKS, turn **Depth** down to 0% and up again while it plays: the system fades out to the plain pattern and back.

Links, their depth and the bounce settings are saved with the pattern (project format 6). Kettle 0.5 and earlier projects load with no links.

---

## 13. Performance mode (PERF)

Press **ARP**. The white keys act **while held**; **SHIFT + key latches** it on (the key again turns it off). Leaving PERF (HOME or SEQ) turns off whatever is latched.

| Key | Name | Does |
|---|---|---|
| 1–7 | rolls | Roll the track at the roll rate (PERF: MORE page). While a roll is held, KNOB 3 sweeps its rate (below). On the PERFORM 2 pages these keys are DRILL (below). |
| 8 | **FILL** | Every step plays whatever its chance; FILL conditions play |
| 9 | **RPT** | **Repeat:** the sequencer loops its position at the repeat length (1/4, 1/8, 1/16, 1/32), starting on that grid. When you let go, the pattern carries on where it would have been. |
| 10 | **FRZ** | **Freeze:** the FX bus's delay loops what it holds (input off, feedback 100%). Needs the FX bus on DELAY; the longest loop is about 0.74 s. |
| 11 | **T.RV** | **Throw:** the selected track's reverb send goes to full while held; its tail rings out after |
| 12 | **T.FX** | **Throw** to the FX bus (FX send full) |
| 13 | **FB+** | **Feedback push:** delay feedback to 0.95 (phaser, flanger: their most) |
| 14 | **STP** | **Tape stop:** tempo and every voice's pitch slow to a stop over the tape time, then silence. Let go: they spin back up in 0.25 s. |
| 15 | **LP~** | Sweeps the DJ filter to its low-pass end over a bar (back in a quarter bar on release) |
| 16 | **HP~** | The same to the high-pass end |
| black 1–7 | | Mutes (SHIFT + key: unmute that track and those before it) |
| bar keys | | The repeat length: 1/4, 1/8, 1/16, 1/32 |

The ALGORITHM encoder selects the track the throws act on (shown as THROW on screen).

The screen shows all 16 keys (lit: on, outlined: latched), the repeat length, the throw track, the DJ filter's position and, by name, the effects that are on.

### PERF pages (SELECT)

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
|---|---|---|---|---|
| PERFORM | Tempo | DJ filter | Repeat | Morph |
| MACROS | Macro 1 | Macro 2 | Macro 3 | Macro 4 |
| MACRO SETUP | Macro (1–4) | Target | Min | Max |
| PERFORM: MORE | Roll rate | Shuffle | All-voice sample rate | Tape time |
| PERFORM 2 | Tempo | DJ filter | Repeat | Morph |
| PERFORM 2: SETUP | Drill | Jitter | Cuts | Pump |
| PERFORM 2: SETUP 2 | Comb (note) | Comb fb | Crush | Reverse (length) |

- **DJ filter:** off in the middle; left a low-pass on the whole output, right a high-pass.
- **Macros:** each macro knob moves one parameter between its Min and Max (Max below Min turns it the other way). Set them up on MACRO SETUP: pick the macro, its target (any lockable parameter), Min and Max. Turning a macro while recording writes locks, as any knob does.
- **Tape time:** 0.2 to 4 seconds.
- **Roll sweep:** while a roll key is held (or latched) on the PERFORM page, KNOB 3 turns from Repeat into **Roll**: a smooth sweep from 1/4 to 1/192, off the grid. Turn it up while holding a kick for a drum-and-bass build, or down for a stutter that slows to a stop. SHIFT + KNOB 3 sets the gridded roll rate instead. Setting the roll rate on PERFORM: MORE puts rolls back on the grid. The swept rate is not saved.

Macros, the repeat length and the tape time are saved with the project. The DJ filter starts in the middle at power-on.

### PERFORM 2: the second bank

Turn SELECT on to **PERFORM 2**. While a PERFORM 2 page is on screen, the 16 white keys play a second set of effects, picked with Aphex Twin, Autechre, microhouse and dub techno in mind. The black keys (mutes) and the bar keys (repeat length) work as before. Hold a key, or latch it with SHIFT, just as on the first bank. Effects latched in either bank keep running when you change pages, and a key always lets go in the bank it was pressed in.

| Key | Name | Does |
|---|---|---|
| 1–7 | **DRILL** | The track's hits become random fast ratchets: 2 to 8 hits at 1/32, 1/48 or 1/64, each with a random velocity and a pitch step of up to ±2 semitones. Drill'n'bass on any track. |
| 8 | **HLF** (HALF) | Half time: the sequencer at half speed, the sounds unchanged |
| 9 | **JIT** (JITTER) | Each hit lands late by a random amount, up to a 32nd; a flam's hits move together |
| 10 | **CUT** (CUTS) | The output is cut in and out on a random 1/32 pattern, with tiny fades: clicks and cuts |
| 11 | **SHT** (SHORT) | Every voice's decay cut to a click |
| 12 | **PMP** (PUMP) | Every track but D1 ducks at each D1 hit and comes back over an eighth note: sidechain pumping |
| 13 | **RES** (COMB) | A tuned comb resonator on the output: the drums ring at the Comb note, like a chord on every hit |
| 14 | **SPC** (SPACE) | The reverb's tail held so it keeps going (a pad) and the dry sound dipped. Raise the reverb sends first. |
| 15 | **CRS** (CRUSH) | Fewer bits and a lower sample rate on the output |
| 16 | **REV** (REVERSE) | The last 1/16, 1/8 or 1/4 (the Reverse setting, 0.25 s at most) played backwards, over and over while held |

**PERFORM 2: SETUP** and **SETUP 2** set how strong each one is: **Drill** (how many hits a drill can have), **Jitter** (the most it delays), **Cuts** (how many 32nds are cut), **Pump** (its depth), **Comb** (its note, C1–C7) and **Comb fb** (how long it rings), **Crush** (bits and rate together), **Reverse** (1/16, 1/8, 1/4). They are saved with the project.

COMB, CUTS and CRUSH act on the whole output after the DJ filter, in that order; REVERSE plays what came out of them. PUMP, SHORT and DRILL act on the voices, and HALF and JITTER on the sequencer, so MIDI out follows them too.

---

## 14. Saving

Press **SAVE** for the save page, SAVE again for the load page (SHIFT + SAVE opens load directly). KNOB 1 chooses the type, KNOB 2 (or SELECT, PRESETS, or a white key in the bank on screen) the slot.

| Type | Slots | Save | Load |
|---|---|---|---|
| KIT | 64 | OCT+, name it, OCT+ | OCT+ |
| PATTERN | 32 | a white key saves to that pattern at once | white key / OCT+: queue it |
| SONG | 64 | OCT+ | OCT+ |
| MORPH | 64 | OCT+, name it, OCT+ (saves the morph mixture you hear) | OCT+: load as the morph target |
| PROJECT | 4 | OCT+, name it, OCT+ | OCT+ |
| FACTORY | the built-in bank (private builds) | – | OCT+: into the sound being edited; ALL > SLOTS: the first 64 into the kit slots |

**Naming:** KNOB 1 the letter's position, KNOB 2 the letter, KNOB 3 upper / lower case. OCT+ saves, OCT− alone goes back.

### Projects

A project is everything: all 64 kits, the 32 patterns, the 64 songs, the settings, the sound as it is, the morph target, the macros. Kettle keeps **four projects** in the FM-1's flash.

**The session saves itself.** When AUTOSAVE is on (CONFIG → DEVICE), Kettle saves the session while stopped, once 8 seconds have passed since you stopped or last changed something and the sound (reverb and delay tails too) has died away. It loads it at power-on. The FM-1 runs on a battery, so this means nothing is lost when it switches off. If something keeps sounding (a frozen delay, pads played live), it saves anyway after 30 seconds of rest.

Writing to flash stalls the FM-1's processor for a moment, so every save (automatic or from the SAVE page) fades the output out, writes, and fades it back in: a save while something sounds is a short gap, not a click or noise. (Before 0.5.1 the save could come right after a stop and chopped the reverb tail into a burst of noise.)

A project has 48 KB. A typical one uses a fraction of that (a busy test project, every pattern 32 steps long on all tracks with every kit edited, takes about 33 KB), but a completely dense one can be too big: saving then shows **PROJECT TOO BIG** and nothing is written. If the automatic session save fails this way, the screen shows **AUTOSAVE: PROJECT TOO BIG** and it doesn't try again until something changes. Clearing unused patterns or tracks makes room.

Projects saved by older Kettle versions load in newer ones (the first 32 of 0.1's 64 patterns). Projects don't load in older versions than the one that saved them.

### Backing up, restoring and LXR-02 kits

The **library page** (`library/` on Kettle's site, from 0.7) connects to the FM-1 over USB MIDI in Chrome or Edge. It can:

- **Back up** every project and the session to one file on the computer (`.ktbk`), or download one project (`.ktp`). Kettle's own updates keep your projects. Installing V15 or another firmware may not, so back up first.
- **Restore** a backup or a project file, into the slots it came from or into a chosen slot, and load it. The old project stays in place until the restore has been written and checked, so pulling the cable halfway loses nothing.
- **Import LXR-02 kits:** pick .SND files, or a whole project folder from an LXR-02 SD card (PROJ00 …). They fill the kit slots from the slot you choose, leaving out Init and empty kits. Then save the project (SAVE → PROJECT), or let AUTOSAVE keep them in the session.
- **Export kits** as LXR-02 .SND files (the LXR's parameters only).

While a restore is being written, the output is silent and saving is held off. If the computer stops sending, Kettle gives up after 10 seconds.

The emulator has the same functions under **Library**, below the panel.

**Built-in kit bank (private builds only).** The LXR-02's factory and Hrtl kits can't be shipped with Kettle, but you can build them into your own copy: `KETTLE_KITS_DIR=folder:folder:… ./build.sh` (and `web/emu/build.sh`), with folders of .SND files from your LXR-02 SD card or its downloads. Such a build:
- shows its version as "v0.7+K";
- starts a fresh unit with the bank's first 64 kits in the kit slots;
- adds a **FACTORY** type on the LOAD page. OCT+ loads the chosen kit into the sound you're editing (SAVE → KIT keeps it in a slot); the last entry, **ALL > SLOTS**, puts the first 64 into the slots.

The site builder refuses to publish a "+K" build.

---

## 15. MIDI

**In:** USB MIDI and the TRS MIDI jack. **Out:** USB MIDI (the FM-1 has no MIDI out jack).

- **Notes:** each track has a MIDI channel (CONFIG: MIDI CHANNELS). A note on a track's channel plays it. With the track's MIDI note set to ANY, it plays at the note's pitch; with a note set, only that note triggers it. Notes on the **global channel** play the selected track.
- **Notes out:** the sequencer sends each track's hits on its channel.
- **CC:** CC *n* sets parameter *n* − 1, as on the LXR. **NRPN** (CC 99 / 98, data entry CC 6) reaches every parameter: NRPN *n* sets parameter *n* + 128. Kettle's own parameters are listed [below](#kettles-parameters-by-nrpn).
- **CC 0** loads a kit. **Program change** 0–31 on the global channel queues a pattern; Kettle sends one when its pattern changes.
- **Clock out:** always on, 24 PPQ, with start and stop.
- **Clock in:** turn the tempo all the way down to **MIDI**. Kettle then follows incoming clock, start, continue and stop. The header shows MIDI and the measured tempo.
- **Filters** (CONFIG: MIDI): what is received and sent: N notes, R clock and start/stop, C CC and NRPN, P program change.
- **Route:** TRS>USB forwards the jack's input to USB (soft thru).

---

## 16. Settings (CONFIG) and the CPU meter

GLO three times (past Mix and the mixer) opens CONFIG. Turn SELECT for its pages:

| Page | Knobs |
|---|---|
| SEQUENCER | Tempo, Shuffle, Follow (the screen follows the playhead), Kit change (AUTO: patterns load their kit) |
| MIDI | Global channel, TX filter, RX filter, Route |
| MIDI CHANNELS (2 pages) | Each track's channel |
| MIDI NOTES (2 pages) | Each track's note (ANY or a note) |
| DEVICE | LEDs (OFF / LOW / HIGH glow), Autosave, Accel (fast knob turns move further), Colours (screen palette) |

### The CPU meter

The bottom of every CONFIG page shows the audio engine's load: **CPU** (the average), **PEAK** (the highest since CONFIG was opened) and, if it ever happens, **LATE** (audio buffers that were computed too late, counted since power-on: these would be heard as clicks). The text turns red above 85%. A buffer can be late because the engine ran out of time, or because something else held the processor; saves to flash no longer count (0.5.1), since they are not the engine's load.

To measure, open CONFIG while a dense pattern plays with the FX bus and reverb on, and note the numbers. The browser emulator always shows 0%, because it doesn't model CPU time.

### The battery

At the right end of the CPU line: the battery's charge as three bars (as the stock firmware reads it), one bar left in red, an empty battery as a red outline. On USB power the FM-1 is charging and it shows **USB** instead.

---

## 17. Reference tables

### SHIFT + white keys (outside PERF)

| Key | Function |
|---|---|
| 1–7 | Play that track's voice |
| 8 | KIT: load a kit |
| 9 | MORPH: load the morph target |
| 10 | SONG page |
| 11 | PROJECT: load a project |
| 12 | CONFIG |
| 13 | RELOAD the kit and pattern as saved |
| 14 | LAST: set the track's last step (again: all tracks) |
| 15 | FX |
| 16 | PATTERN page |

In PERF, SHIFT + a white key latches it instead.

### Other combinations

| Combination | Does |
|---|---|
| SHIFT + track key | Mute / unmute |
| SHIFT + bar key | Track length 16 / 32 / 48 / 64 |
| COPY + bar key | Copy the shown bar (on the grid: every track) |
| COPY + track, track | Copy a track |
| COPY + pattern, pattern | Copy a pattern (pattern page) |
| SHIFT + COPY | CLEAR (again to confirm) |
| SHIFT + PLAY | Play the song from the start |
| SHIFT + REC | Record options |
| SHIFT + SAVE | Load page |
| OCT− + OCT+ held 5 s | The chip's UBOOT mode (recovery with the Transporter only) |
| Hold step + turn knob | Lock |

### Kettle's parameters by NRPN

Six numbers per voice-based setting, in the order D1 D2 D3 SN CY HH.

| NRPN | Parameter |
|---|---|
| 174–179 | FX send |
| 180–185 | Reverb send |
| 186, 187, 188, 189 | Reverb size, damping, level, model (0 room, 1 spring, 2 hall) |
| 190–195 | Bits (0 off … 15 = 1 bit) |
| 196–201 | Waveshaper (0 LXR, 1 fold, 2 clip, 3 asym, 4 rectify) |
| 202–207 | Tilt (64 flat) |
| 208–213 | Choke group (0 none, 1–4) |
| 214–219 | Random pitch |
| 220–225 | Random decay |
| 226–231 | Random filter |
| 232–237 | Random pan |
| 238–243 | Cross-mod source (0 none, 1–6) |
| 244–249 | Cross-mod mode (0 duck, 1 filter, 2 ring) |
| 250–255 | Cross-mod amount |
| 256–261 | Model (0 LXR, 1 skin, 2 bar, 3 bell, 4 plate, 5 tube; D1–D3 only) |
| 262–267 | Model decay |
| 268–273 | Model damping |
| 274–279 | Model material (64 even) |
| 280–285 | Model position |
| 286–291 | Model exciter |
| 292–297 | Model tension |

The LXR's own parameters keep their CC and NRPN numbers.

### Capacities

| | |
|---|---|
| Voices / tracks | 6 / 7 |
| Patterns | 32, each 7 tracks × up to 64 steps |
| Locks | 2 per step |
| Kits | 64 |
| Songs | 64 × 64 rows |
| Projects | 4, plus the automatic session |
| Sample rate | 44.1 kHz |

---

## 18. How Kettle differs from the LXR-02

What changed in the port:

- **One stereo output.** The FM-1 has one stereo output, so the LXR's two stereo pairs and four mono outputs all mix into it (a mono output to its side). Kits keep their routing settings.
- **32 patterns** instead of 64, to make room for the new features.
- **No SD card.** Kits, patterns and songs live in projects in the FM-1's flash (four projects plus the automatic session).
- **The FX bus output is a send.** "Out = FX BUS" became each voice's FX send; old kits load with it at 127.
- **No CV/gate and no MIDI out jack.** MIDI goes out over USB.
- **Fixed-point sound engine.** The FM-1 has no floating-point unit, so the LXR's DSP was rewritten in fixed point. Tests compare every voice with the LXR's own code: levels match within 0.1 dB and tonal voices are near identical. Noise-based sounds differ only because the noise generator differs.
- **Sequencer details:** each track's flams and shifts only affect that track (the LXR's could swallow other tracks' notes); a step sends one MIDI note; MIDI clock out stays on the unshuffled grid; the faster and triplet roll rates play at their rate.

What Kettle adds:

- **Sound:** FX and reverb sends per voice; three reverbs; waveshaper choice; bits; tilt EQ; random per hit; choke groups; cross modulation; phaser and flanger.
- **Performance:** PERF with fill, repeat, freeze, throws, feedback push, tape stop, filter sweeps, a DJ filter and four macros.
- **Sequencing:** trig conditions, ratchets, per-track clocks, generators (mutate, Turing machine, cellular automata, Grids), dice, commit and undo, and copying a bar across all tracks.
- **Safety:** the automatic session save, a CPU meter, and backup and restore over USB (the library page).

---

## 19. Troubleshooting

| Symptom | Try |
|---|---|
| A change to the pattern disappeared after switching patterns | Unsaved pattern changes are dropped on a switch (as on the LXR). Save with SAVE → PATTERN first, or save the project. |
| No sound from a voice | Is it muted (dark key)? Its volume (mixer)? FX send at 127 with the FX bus on a quiet setting? A choke group cutting it? Cross mod DUCK? |
| FREEZE does nothing | The FX bus must be set to DELAY, and something must be sent to it (FX send or THROW FX). |
| Tempo shows MIDI and nothing plays | Tempo is turned to MIDI: Kettle waits for MIDI clock. Turn the tempo up. |
| A generator's track keeps changing | That's the generator. Freeze it, COMMIT what you like, or set it to OFF. |
| AUTOSAVE: PROJECT TOO BIG | The session doesn't fit a project slot. Clear unused patterns or tracks. |
| Clicks or LATE on the CPU meter | If PEAK is well below 100%, something else held the processor; please report what you were doing. If PEAK nears 100%, the engine is out of time: turn off the reverb (sends to 0), use a simpler FX type, or fewer cross-mods, and please report the CPU numbers. |
| A burst of noise a few seconds after stopping | Fixed in 0.5.1 (the autosave chopped the reverb tail). If you still hear it, turn AUTOSAVE off and please report it. |
| An install stopped half way | Plug the FM-1 in and press Install again: it finishes the write ([section 1](#if-something-goes-wrong)). |
| The FM-1 doesn't start Kettle | After two failed starts the boot guard puts it in UBOOT. Recovery from there needs the FM-1-transporter ([section 1](#if-something-goes-wrong)). Please report it. |

---

## 20. Licence and credits

Kettle is free software under the **GNU General Public License, version 3** (GPL-3.0-only). See `LICENSE` and `NOTICE.md`.

It is built from:

- **The LXR-02 firmware**, © 2012–2026 Erica Synths, GPL-3.0, itself built on Julian Schmidt's original Sonic Potions LXR: the sound engine, parameters, kit format, sequencer, MIDI and front-panel functions.
- **Felucca**, © 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, GPL-3.0: the FM-1 hardware layer, boot, screen, graphics, USB, storage, updater, browser emulator and the reverb.
- **Grids**, © 2011–2012 Emilie Gillet, GPL-3.0-or-later (used under GPL-3.0): the drum map and its reading.
- **Inter Tight** (the screen's font) and **DotGothic16** (the emulator page), SIL Open Font License 1.1.

"Erica Synths" and "LXR-02" are trademarks of Erica Synths; "M-VAVE" and "FM-1" are trademarks of their owner.

### Building and testing

See `README.md`: building the firmware (`./build.sh`), the browser emulator (`web/emu/build.sh`), and the host tests (`render_test`, `ui_test`, `compare_test`).
