Goal: Create a microsound module for Elixir, inspired by the work of Curtis Roads. 

Your ten MICRO voices already cover a lot of the basic sound-generation territory: impulses, click trains, digital noise, short bursts, resonant grains, and chirps.

But Roads' most interesting ideas aren't necessarily about generating different types of microsounds. They're about organizing tiny sounds into larger textures, rhythms, clouds, and evolving structures.

That's where I think Elixir could become much more interesting for the clicks-and-cuts, Mille Plateaux, Autechre, and microhouse territory you're targeting.

I'd separate the possibilities into new MICRO voices and new ways of controlling existing voices.

## 1. New MICRO voices inspired by Roads

These would complement your existing ten without duplicating too much functionality.

| Voice    | Description                                                                                       | Colour knob                         |
| -------- | ------------------------------------------------------------------------------------------------- | ----------------------------------- |
| PULSELET | A tiny windowed sine or cosine pulse. More rounded and tonal than CLICK.                          | Soft, rounded → sharp, bright       |
| FORMANT  | A very short pitched grain with a resonant spectral peak, inspired by granular formant synthesis. | Dark, vowel-like → bright, metallic |
| SHARD    | A short fragment of inharmonic partials, like a microscopic bell or broken glass.                 | Harmonic → inharmonic               |
| SWARM    | A compact cloud of overlapping grains with slight pitch and timing differences.                   | Unified → scattered                 |
| SPLINTER | One impulse fractures into smaller, irregularly spaced impulses.                                  | Single attack → fragmented          |
| WAVELET  | A few cycles of a pitched waveform inside a short envelope.                                       | Smooth → rough/complex              |

My favorites are FORMANT, SPLINTER, and WAVELET.

PULSELET is also worth considering because Roads' pulsar synthesis uses brief waveforms and silence to create pitched and rhythmic structures. However, its greatest potential comes from repeating and modulating the pulses, not just treating them as individual drum hits.

SWARM could be implemented as a voice, but I think it would be even more powerful as a processing or sequencing feature that works on all your existing MICRO types.

## 2. The more exciting ideas: microsound behaviors

These are drawn from the granular, pulsar, and particle-organization concepts Roads explores in Microsound and his related synthesis work.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://djvu.online\&sz=32)

2001+2



| Feature     | What it does                                                             | Value for Elixir                                              |
| ----------- | ------------------------------------------------------------------------ | ------------------------------------------------------------- |
| CLOUD       | Generates a cluster of grains around each sequencer trigger.             | Turns one step into a swarm of micro-events.                  |
| SCATTER     | Distributes grains irregularly across a time window.                     | Organic, fractured rhythms instead of conventional ratchets.  |
| PULSAR      | Repeats tiny windowed waveforms with adjustable silence between them.    | Transitions from separate clicks to buzzes and pitched tones. |
| GLISSON     | Gives individual grains tiny pitch glides.                               | Insect-like chirps, liquid digital sounds.                    |
| TENDENCY    | Constrains randomized grains to a changing frequency or amplitude range. | Controlled evolution rather than arbitrary randomness.        |
| FILL        | Controls how much grains overlap.                                        | Morphs between sparse clicks and continuous texture.          |
| MICROFILTER | Changes filter frequency or resonance separately for each grain.         | Creates animated, granular timbres.                           |
| GRAINLET    | Uses short, formant-rich particles for vocal or spectral textures.       | Strange, synthetic percussion.                                |

Some are established Roads terminology; others, such as SCATTER and MICROFILTER, are my proposed Elixir names for related techniques.

### The most important concept: rhythm becomes timbre

Roads' pulsar synthesis is particularly interesting because changing the repetition rate can transform discrete rhythmic events into a continuous pitched sound.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://aes.org\&sz=32)

AES E-Library+1



Illustrative pulsar behavior

Pulse rate

2 Hz

Distinct rhythmic clicks

Conceptual visualization, not an audio simulation. The perceived result also depends on waveform, pulse width, and envelope.

This could become a fantastic performance parameter for Elixir, especially with a global LFO modulating the rate.

## 3. My preferred implementation: a shared GRAIN behavior

Rather than adding six new voice types and substantially expanding the model menu, I'd implement a lightweight grain generator that works with your existing MICRO voices.

One sequencer step could generate a short collection of sound particles, with these four parameters:

| Knob | Parameter | Behavior                                      |
| ---- | --------- | --------------------------------------------- |
| 1    | COUNT     | Number of particles, 1–16                     |
| 2    | SPREAD    | Time window for the particles, 0–250 ms       |
| 3    | DRIFT     | Variation in particle timing, pitch, or color |
| 4    | SHAPE     | Even spacing → clustered → scattered          |

For example, one CLICK could turn into sixteen irregular clicks. A GRAIN voice could become a tiny cloud of resonant particles. A CHIRP could become an insect-like chorus.

This would complement your existing sequencer ratchets: ratchets are musical step subdivisions, while grain clusters can operate independently at the millisecond level.

Importantly, your existing DENSITY parameter should remain separate. It controls which sequencer events are heard; grain COUNT controls how many particles occur inside an event.

## 4. Additional techniques that would make the system distinctive

Grain-by-grain parameter variation. Instead of retriggering exactly the same sound, each particle receives a small variation in pitch, color, duration, or amplitude. This is especially effective for BITS, CRUNCH, and GRAIN.

Grain envelopes. A switch between rectangular, triangular, and rounded windows would dramatically change the sound of CLICK, BURST, and WAVELET. A short lookup table could make this relatively inexpensive.

Tendency masks. Instead of completely random values, give each cloud a trajectory: grains gradually brighten, become quieter, or accelerate. This follows Roads' approach of defining broad boundaries and letting an algorithm generate the individual particles.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://erickuehnl.com\&sz=32)

Eric Kuehnl



Microscopic time stretching. Repeat or overlap tiny portions of a synthesized grain to stretch its duration without simply lengthening its envelope. This would be a more substantial feature, but it could generate excellent frozen digital textures.

Cloud freezing. Generate a randomized particle sequence once, then repeat it identically until the user requests a new one. This is my Elixir-specific extension, rather than a named Roads technique. It would be great for turning random glitches into recognizable hooks.

## 5. What I'd prioritize for the FM-1

1

SPLINTER voice

Low

Adds a distinct broken-impulse sound with minimal DSP overhead.

2

WAVELET / PULSAR voice

Low–medium

Introduces the rhythm-to-pitch continuum missing from the current set.

3

GRAIN cluster behavior

Medium

Multiplies the usefulness of every existing MICRO voice.

4

Per-grain variation

Low–medium

Makes clusters feel alive rather than like identical retriggers.

5

Tendency / cloud trajectories

Medium

Adds the evolving, generative quality associated with Roads' compositional techniques.

The CPU estimates are relative and would need validation on the FM-1. I'd use a fixed-size particle pool, deterministic random-number generation, and no dynamic allocation in the audio callback. Grain overlap and simultaneous tracks would need explicit limits.

One final idea: make GRAIN cluster parameters destinations for your global LFOs. Modulating COUNT, SPREAD, or particle variation would allow a single programmed snare hit to gradually dissolve into an irregular cloud, then reform into a crisp rhythmic event.

That would be a particularly compelling intersection of Roads' microsound techniques and Elixir's existing generative sequencer.

My strongest recommendation is to build the shared grain behavior before adding lots of new MICRO types. You already have ten interesting sources; the bigger opportunity is to give them the ability to organize themselves into microsound compositions.