
# Elixir — Cast Spells

## Overview

Cast Spells is Elixir's guided randomization and mutation system. Each spell is a distinct creative algorithm for transforming drum voices, patterns, effects, or interactions between tracks.

Spells should produce musically useful results across a range from subtle to extreme. Unlike unrestricted randomization, each spell uses weighted parameter ranges, correlated changes, and constraints appropriate to its character.

### Core principles

- **Musical first:** Prefer coherent combinations of parameters over independent random values.
- **Non-destructive exploration:** Every cast can be reversed.
- **Track targeting:** Use the FM-1 keyboard to select affected tracks.
- **Immediate audition:** Casting should work while the sequencer is running, without restarting playback.
- **Repeatability:** Recipes can be saved and recast with different random seeds.
- **Performance:** Spells should be useful for both sound design and evolving live patterns.
- **Resource awareness:** Avoid combinations of voice models and effects that exceed the FM-1's processing budget.

## Interface

Elixir has four main knobs and three proposed spell pages.

### Page 1 — Grimoire

| Knob | Parameter | Description |
|---|---|---|
| K1 | Spell | Select built-in spell or saved recipe |
| K2 | Variation | Exact repeat through increasingly different interpretations |
| K3 | Remix Scope | Sound, pattern, or both |
| K4 | Recipe Mode | Recall, Recast, or Remix |

### Page 2 — Cast

| Knob | Parameter | Description |
|---|---|---|
| K1 | Sound Mutation | Amount of voice parameter mutation |
| K2 | Pattern Mutation | Amount of sequencer mutation |
| K3 | Density | Sparseness or complexity of generated events |
| K4 | Chaos | Musical constraints versus extreme randomness |

A mutation amount of zero should disable that category. Sound-only and pattern-only spells may repurpose inactive controls where appropriate.

### Page 3 — Character

| Knob | Parameter | Description |
|---|---|---|
| K1 | Sound Character | Spell-specific timbral direction |
| K2 | Rhythm Character | Spell-specific rhythmic behavior |
| K3 | Evolution | Static versus gradual transformation |
| K4 | Protection | How strongly original musical anchors are preserved |

The Character controls should have spell-specific labels and meanings.

### Page 5 — Motion (0.19-test11)

| Knob | Parameter | Description |
|---|---|---|
| K1 | Motion | How far the targets' speed and direction part: SAME (all forward at the pattern's speed), ONE (one track x2, 1/2, 3/2, backwards or ping-pong), FEW (two; 3/4 and 4/3 too), MANY (about half; 2/3 to x3, random and the second ping-pong), FREE (as the spell rolls it) |
| K2 | Lengths | The lengths a cast may give: BARS (16, 32, 64), EVEN (12, 24 too), ODD (7, 14, 15, 20 too) |

Shared by every spell, like LIMITS (page 4), and saved with the project.

### Keyboard and actions

- Keyboard keys select target tracks.
- Selected tracks are visibly indicated.
- Oct+ flashes when the spell can be cast.
- Press Oct+ to cast without interrupting playback.
- Oct− performs **Reverse Spell**, restoring the exact pre-cast state.
- A cast must not execute when no valid target tracks are selected.
- Changing the selected spell or its settings must not modify the pattern until the user casts.

### Saving and recalling

A saved spell recipe contains:

- Spell type
- All spell control values
- Targeting preferences
- Parameter constraints and protection settings
- Optional random seed

Three recall behaviors are proposed:

- **Recall:** Restore an exact saved result, if a state snapshot exists.
- **Recast:** Run the saved recipe again with a new random seed.
- **Remix:** Modify the recipe's settings or constraints before generating.

A recipe is not the same as a state snapshot. Exact recall requires saving the resulting voice and pattern state, not just the generation settings.

---

# The Spells

## 1. Poltergeist

**Theme:** Unstable, metallic, supernatural percussion.

**Primary domain:** Sound.

### Musical goal

Transform familiar drum voices into strange metallic hits, rattles, hollow knocks, unstable FM tones, and ghostly percussion while retaining their rhythmic placement.

### Candidate parameter targets

- Oscillator waveform, coarse and fine tuning
- FM amount, frequency, waveform, and mode
- Transient waveform, volume, and frequency
- Amp envelope decay and slope
- Pitch envelope depth and decay
- Filter frequency, resonance, and type
- Waveshaper type, drive, bits, and tilt
- Selected Rings and Plaits timbral parameters

### Generation behavior

- Favor inharmonic oscillator and FM relationships.
- Correlate high FM amounts with appropriate filtering and envelope changes.
- Use short decays for sharp metallic hits and longer decays for resonant sounds.
- Prefer changing existing voice parameters before switching synthesis models.
- Keep the programmed rhythm intact by default.

### Character controls

- K1 — **Metallicity:** Harmonic to strongly inharmonic sound.
- K2 — **Rattle:** Clean strikes to unstable repeated or noisy transients.
- K3 — **Haunting:** Static timbre to slow modulation and drifting resonances.
- K4 — **Preserve:** Protect original pitch, decay, and drum identity.

### Safeguards

- Avoid excessive simultaneous resonance and drive.
- Preserve a useful amplitude envelope.
- Limit unintended pitch changes on bass drums.

---

## 2. Skeleton

**Theme:** Stripped-down, skeletal rhythmic structures.

**Primary domain:** Pattern.

### Musical goal

Reduce complex rhythms to sparse, compelling grooves with carefully placed accents, gaps, and occasional surprises.

### Candidate parameter targets

- Step activation
- Velocity
- Probability/chance
- Conditions
- Microtiming/shift
- Flam
- Ratchet count and rate
- Optional track length

### Generation behavior

- Start from the current pattern or generate a new sparse structure.
- Prefer strong beats and musically important accents.
- Remove weaker offbeats before essential rhythmic anchors.
- Introduce occasional ghost notes or syncopation at higher settings.
- Use probability to make sparse patterns subtly variable.

### Character controls

- K1 — **Emphasis:** Which rhythmic accents dominate.
- K2 — **Syncopation:** Straight pulse to displaced and offbeat hits.
- K3 — **Variation:** Stable repetition to occasional conditional events.
- K4 — **Anchors:** Preserve kick, snare, or selected programmed steps.

### Safeguards

- Avoid unintentionally producing a completely silent pattern.
- Preserve selected anchors unless explicitly allowed to remove them.
- Keep voice sound parameters unchanged by default.

---

## 3. Glitch Storm

**Theme:** Fragmented IDM, digital errors, rapid edits, and chaotic percussion.

**Primary domain:** Sound and pattern.

### Musical goal

Create unpredictable, high-energy rhythmic mutations reminiscent of glitch, drill-and-bass, and experimental electronic percussion.

### Candidate parameter targets

**Sound**
- Oscillator pitch and fine tuning
- Transient parameters
- Amp decay
- Filter frequency and resonance
- Sample-rate reduction
- Bit reduction
- Drive and waveshaping
- Optional MICRO model selection

**Pattern**
- Step activation and chance
- Conditions
- Microtiming
- Flam
- Ratchet hits, rate, fade, and shape
- Velocity

### Generation behavior

- Generate clusters of rapid events separated by gaps.
- Use short, sharply differentiated sounds.
- Favor correlated ratchet and decay changes.
- Mix deterministic rhythmic structures with irregular fills.
- At high chaos, introduce aggressive timing and timbral discontinuities.

### Character controls

- K1 — **Corruption:** Clean percussion to heavily degraded digital sounds.
- K2 — **Fragmentation:** Occasional glitches to dense bursts and cuts.
- K3 — **Instability:** Repeating edits to evolving, unpredictable variations.
- K4 — **Groove Lock:** Preserve the underlying beat and selected anchors.

### Safeguards

- Cap event and ratchet density.
- Avoid excessive peak accumulation.
- Preserve at least some recognizable rhythmic structure at moderate settings.

---

## 4. Haunted Dub

**Theme:** Dark dub techno, filtered echoes, spacious percussion, and unstable delays.

**Primary domain:** Sound and effects.

### Musical goal

Generate warm, spacious, evolving dub percussion with resonant filters, delay feedback, subtle modulation, and atmospheric tails.

### Candidate parameter targets

- Amp decay
- Filter frequency, resonance, and type
- Drive, waveshaping, and tilt
- Pan
- Rings resonance-related parameters
- Per-track FX sends
- Delay time, feedback, tone, modulation, and saturation
- Reverb size and damping

### Generation behavior

- Favor restrained, darker timbres.
- Correlate decay, filtering, and send levels.
- Use moderate delay feedback with carefully controlled saturation.
- Keep bass drums comparatively dry.
- Introduce slow tonal and spatial changes.

### Character controls

- K1 — **Darkness:** Bright and clear to heavily filtered and warm.
- K2 — **Echo:** Dry rhythmic accents to spacious delayed patterns.
- K3 — **Drift:** Static echoes to evolving modulation and movement.
- K4 — **Dry Anchor:** Protect the kick and essential rhythmic transients.

### Safeguards

- Clamp delay feedback to safe ranges.
- Prevent runaway feedback and excessive output levels.
- Account for shared FX buses so a cast does not unexpectedly disrupt unrelated tracks.

---

## 5. Forbidden Ritual

**Theme:** Full-system mutation and unpredictable transformation.

**Primary domain:** Everything.

### Musical goal

Explore the most extreme combinations Elixir can produce, transforming voice synthesis, sequencing, modulation, and effects together.

### Candidate parameter targets

- Voice model selection
- Oscillators, FM, transients, and envelopes
- Filters and waveshaping
- Rings, Plaits, and MICRO parameters
- Steps, velocity, probability, and conditions
- Ratchets, flam, and microtiming
- Track length, clock division, and direction
- Generators and their settings
- LFO routing and modulation
- Cross-modulation and choke configuration
- Links and inter-track behavior
- Effects and sends

### Generation behavior

- Select from broad but valid combinations of parameters.
- Mutate multiple systems simultaneously.
- Allow unusual clock relationships and polymetric patterns.
- Generate new modulation relationships.
- At maximum chaos, permit substantial model and structural changes.

### Character controls

- K1 — **Transmutation:** Degree of synthesis-model and timbral change.
- K2 — **Disorder:** Rhythmic and structural unpredictability.
- K3 — **Possession:** Depth of modulation and inter-track interaction.
- K4 — **Protection:** Restrict which original elements survive.

### Safeguards

- Validate CPU and voice-model budgets.
- Prevent invalid modulation destinations.
- Keep feedback and output levels bounded.
- Snapshot every potentially affected subsystem for exact undo.
- Consider an optional confirmation for unusually destructive structural changes.

---

## 6. Raster Séance

**Theme:** Microsound, clicks-and-cuts, microhouse, and Mille Plateaux-inspired textures.

**Primary domain:** MICRO synthesis and pattern.

### Musical goal

Produce intricate arrangements of clicks, tiny bursts, noise grains, crunchy digital fragments, and rhythmic silence.

### Candidate parameter targets

**Sound**
- MICRO model
- MICRO Length
- MICRO Colour
- MICRO Jitter
- MICRO Reverse
- MICRO Tension
- Pitch
- Filtering
- Sample-rate and bit reduction
- Drive, tilt, and pan

**Pattern**
- Step activation
- Probability and conditions
- Microtiming
- Flam and ratchets
- Track length and clock division
- Direction

### Generation behavior

- Favor very short sound events.
- Generate clusters separated by deliberate silence.
- Use subtle timing displacement for microsound textures.
- Combine dry clicks with occasional resonant or noisy grains.
- Favor cymbal and hat tracks, with optional D1–D3 targets.
- Use density to transition from sparse clicks to complex granular rhythms.

### Character controls

- K1 — **Grain:** Tiny clean clicks to rough and noisy microtextures.
- K2 — **Fragments:** Sparse individual events to clustered cuts and bursts.
- K3 — **Jitter:** Stable microscopic timing to irregular temporal movement.
- K4 — **Backbone:** Preserve selected rhythmic anchors.

### Safeguards

- Respect available MICRO models on each track.
- Avoid excessive high-frequency accumulation.
- Limit simultaneous ratchets and event density.
- Avoid unnecessary model changes on tracks not selected.

---

## 7. Concrete Drift

**Theme:** Minimal techno, restrained variation, and very slow evolution.

**Primary domain:** Subtle sound and pattern changes.

### Musical goal

Create hypnotic patterns that remain recognizable while gradually changing over many bars.

### Candidate parameter targets

- Oscillator fine tuning
- Small FM changes
- Amp envelope decay
- Filter frequency and resonance
- Subtle waveshaping
- Rings or MICRO timbral parameters
- Velocity
- Chance
- Small microtiming offsets
- Occasional ratchets
- Optional track length or clock relationships

### Generation behavior

- Apply small, correlated changes.
- Preserve the main groove.
- Favor low-probability secondary events.
- Introduce changes gradually over 4, 8, or more bars.
- Allow long-term movement without constant dramatic transitions.
- Prefer deterministic variation over unrelated randomness each cycle.

### Character controls

- K1 — **Texture:** Amount of subtle timbral variation.
- K2 — **Movement:** Degree of rhythmic variation.
- K3 — **Drift Rate:** Speed of long-term evolution.
- K4 — **Stability:** How closely the pattern remains tied to its original groove.

### Safeguards

- Avoid abrupt jumps between parameter values.
- Preserve kick and snare anchors by default.
- Keep pitch and timing deviations small at moderate settings.
- Use interpolation or scheduled transitions where appropriate.

---

## 8. Rust Circuit

**Theme:** Industrial percussion, machinery, distortion, and mechanical rhythms.

**Primary domain:** Sound and pattern.

### Musical goal

Generate abrasive, metallic, repetitive percussion with a mechanical character, unstable transients, and aggressive accents.

### Candidate parameter targets

- Oscillator and FM parameters
- Transient waveform and level
- Amp decay
- Filter resonance and overdrive
- Waveshaper, drive, bits, and tilt
- Rings and MICRO models
- Ratchets and flams
- Timing shifts
- Chance and conditions
- Direction
- Links between tracks

### Generation behavior

- Favor metallic, noisy, and distorted sounds.
- Create machine-like rhythmic repetition.
- Use abrupt accents and short bursts.
- Generate interlocking mechanical patterns.
- Introduce controlled rhythmic imperfections.
- Allow different tracks to form related mechanical cycles.

### Character controls

- K1 — **Corrosion:** Clean metal to heavily degraded and distorted sound.
- K2 — **Machinery:** Regular mechanical repetition to complex interlocking rhythms.
- K3 — **Malfunction:** Stable operation to timing slips and erratic bursts.
- K4 — **Foundation:** Preserve the main rhythmic pulse.

### Safeguards

- Prevent clipping from excessive drive and simultaneous hits.
- Bound resonance and feedback.
- Avoid changing unrelated tracks through links without explicit permission.

---

## 9. Glass Cathedral

**Theme:** Resonant, pitched, crystalline, and harmonic percussion.

**Primary domain:** Tuned sound and melodic pattern.

### Musical goal

Generate delicate or dramatic tuned percussion, glassy resonances, shimmering textures, and harmonically related rhythms.

### Candidate parameter targets

**Rings**
- Model
- Decay
- Bright
- Structure
- Position
- Exciter
- Tension

**Plaits**
- Model
- Harmonic
- Timbre
- Morph
- Colour
- Aux
- Env

**Other**
- Pitch and tuning
- Amp and pitch envelopes
- Filter and resonance
- Reverb send
- Notes and velocities
- Probability
- Track clock and direction
- Optional melody generator settings

### Generation behavior

- Favor resonant and pitched synthesis models.
- Select notes from a coherent pitch collection.
- Create relationships between tuned voices.
- Use different decay lengths to establish depth.
- Generate sparse, shimmering accents or denser interlocking melodic percussion.
- Use reverb carefully to emphasize space without losing articulation.

### Character controls

- K1 — **Brilliance:** Dark resonances to bright crystalline timbres.
- K2 — **Harmony:** Single-note percussion to interlocking pitched patterns.
- K3 — **Shimmer:** Static resonances to evolving harmonic movement.
- K4 — **Tuning Lock:** Preserve tonal center and pitch relationships.

### Safeguards

- Respect CPU limits for Rings and Plaits.
- Avoid excessive overlapping resonances.
- Preserve useful pitch relationships.
- Avoid unintentional changes to bass-drum tuning.

---

## 10. Biofeedback

**Theme:** Interdependent tracks and self-organizing rhythmic ecosystems.

**Primary domain:** Modulation, links, and sequencing relationships.

### Musical goal

Make drum tracks influence one another so the pattern behaves like a connected system rather than independent sequences.

### Candidate parameter targets

- LFO waveform, frequency, sync, amount, and destination
- Random per-hit pitch, decay, filter, and pan
- Cross-modulation source, mode, and amount
- Choke relationships
- Inter-track links
- Step chance and conditions
- Ratchets and velocity
- Track length, clock, and direction
- Filter and envelope modulation

### Generation behavior

Generate meaningful relationships such as:

- Kick triggers reduce hat activity.
- Snare events create additional ghost percussion.
- A quiet track becomes more active when another rests.
- One track's accents influence another's decay.
- Different track lengths create changing interaction patterns.
- Cross-modulation introduces controlled timbral dependence.

Prefer a few coherent relationships over a large number of unrelated links.

### Character controls

- K1 — **Coupling:** Strength of inter-track sound and modulation relationships.
- K2 — **Response:** Degree of rhythmic interaction between tracks.
- K3 — **Adaptation:** Static relationships to evolving interactions.
- K4 — **Autonomy:** Preserve independent track behavior versus stronger coupling.

### Safeguards

- Avoid cyclic modulation or trigger relationships that can run away.
- Validate source and destination compatibility.
- Limit the number of active links.
- Keep all interactions deterministic or reproducible from a seed when possible.
- Preserve selected track independence when requested.

---

# Shared Implementation Requirements

## Randomization

Each spell should use its own weighted parameter distributions rather than uniform random values.

Parameter changes should be correlated where musically appropriate. For example:

- Higher FM amounts may require shorter decays or reduced levels.
- Dense ratchets may require shorter envelopes.
- Greater delay feedback may require lower send or drive levels.
- Higher resonance may require gain compensation.
- More complex patterns may benefit from increased rhythmic silence elsewhere.

## Mutation strength

Mutation should be calculated relative to the current state where possible.

At low strengths, preserve most existing values and introduce small changes.

At high strengths, allow wider ranges, different models, and structural changes according to the selected spell.

## Density

Density should have a predictable relationship to musical complexity.

Avoid making the Density control merely another independent randomness parameter.

Where possible, reuse Elixir's existing deterministic density behavior so the results remain musically coherent.

## Evolution

Some spells, particularly Concrete Drift and Biofeedback, need time-dependent behavior.

Possible implementation approaches:

1. Generate a static result.
2. Generate multiple states and transition between them over bars.
3. Configure existing LFOs, generators, and probability systems to create ongoing variation.

Prefer reusing existing modulation and sequencing infrastructure rather than implementing expensive new DSP.

## Undo and Commit

Before each cast, save the exact state of every subsystem the spell may modify.

Reverse Spell must restore:

- Voice models and parameters
- Pattern steps and step properties
- Track timing and generator configuration
- LFO assignments
- Links and cross-modulation
- Effects and sends
- Any other changed global or shared settings

Undo should work without restarting playback.

A future Commit operation could accept the current result as a new baseline.

A bounded multi-level cast history would be useful if memory allows.

## Resource and safety constraints

- Validate voice model availability for each selected track.
- Avoid exceeding available CPU or memory.
- Limit maximum event and ratchet density.
- Prevent runaway feedback.
- Prevent invalid modulation destinations.
- Preserve unrelated tracks and shared effects unless explicitly targeted.
- Support reproducible results using a stored random seed.
- Keep casting and undo operations suitable for real-time performance.

## Suggested Development Order

### Phase 1 — Core spell engine

- Spell registry and metadata
- Track targeting
- Weighted randomization
- Parameter constraints
- Oct+ casting
- Oct− Reverse Spell
- Poltergeist
- Skeleton
- Glitch Storm

### Phase 2 — Expanded sound and pattern spells

- Haunted Dub
- Raster Séance
- Concrete Drift
- Rust Circuit
- Glass Cathedral

### Phase 3 — Structural and interactive spells

- Biofeedback
- Forbidden Ritual
- Link and modulation validation
- Resource-budget checks
- Improved evolution behavior

### Phase 4 — Grimoire and persistence

- Save custom spell recipes
- Recall, Recast, and Remix
- Random seed persistence
- Optional exact-result snapshots
- Optional multi-level undo and Commit

---

## 11. Whispering Motes (0.19-test11)

**Description:** Microsound, softly: dust, grains, pops and chirps over a hushed kick.

### Musical goal

Quiet, delicate microsound: MICRO's sounds on every voice but D1, small soft hits in scattered clusters, space around them.

### Generation behavior

- D2 and D3: MICRO's GRAIN, POP, CHIRP or CLICK; the snare: BURST, SNAP, DUST or GRAIN, or half the time its own noise (noise level and mix up, short); the cymbal: DUST, GRAIN or TRAIN; the hats: CLICK, TRAIN, DUST or BITS.
- MICRO's knobs: short lengths, COLOUR from the Grain knob, JITTER with Whisper, a little REVERSE and TENSION.
- D1: the LXR's kick, short, little click, dark, its level 84–98 (always under 100).
- New hits and accents softer than other spells'; clusters, micro-timing and ratchets favoured; a few high melody notes on the toms.
- With every track a target: a bright plate or hall (within LIMITS).

### Character controls

| Knob | Name | Description |
|---|---|---|
| K1 | Grain | Soft dust to bright grain |
| K2 | Scatter | Sparse to scattered |
| K3 | Whisper | Still to shimmering |
| K4 | Anchors | Keeps kick and snare |

---

# Design Direction

Cast Spells should not feel like a collection of unrelated randomize buttons.

Each spell should have a recognizable musical identity, useful default settings, and enough controllable variation to become an instrument in its own right.

The ideal workflow is:

**Choose spell → Select tracks → Shape mutation → Cast → Audition → Commit or Reverse → Recast or Remix**

The goal is to make happy accidents repeatable, controllable, and performable.
