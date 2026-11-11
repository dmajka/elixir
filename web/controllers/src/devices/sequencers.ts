/** Sequencers: no template to build, only instrument definitions to download. web/make_site.py writes them into the
 * site's data/<folder>/ from the remote map (tools/hapax_defs.py, tools/oxi_defs.py) on every build. */
export interface SequencerFile { name: string; title: string; what: string }
export interface Sequencer {
  id: string
  name: string
  folder: string
  lead: string
  files: readonly SequencerFile[]
  /** the setup steps: [what, how], with the remote's channels as set on the page */
  steps: (tracks: number, global: number) => [string, string][]
  /** the files carry 15 and 16; a step for other channels */
  otherChannels: string
  legend?: string
  source: { href: string; text: string }
}

const FM1 = (tracks: number, global: number): [string, string] =>
  ['On the FM-1', `CONFIG → REMOTE: Tracks ${tracks}, Global ${global}. CONFIG → MIDI CHANNELS: D1–OH on 1–7 and MIDI NOTES at ANY (both the defaults) for the drums.`]

export const SEQUENCERS: readonly Sequencer[] = Object.freeze([
  {
    id: 'hapax', name: 'Hapax', folder: 'hapax',
    lead: 'Squarp Hapax instrument definitions, written from the remote map with every build, so their numbers are the firmware’s. Each file sets up one Hapax track; use one or both.',
    files: [
      { name: 'ELIXIR_DRUMS.txt', title: 'Drums', what: 'A drum track: lanes 1–7 play D1 to OH on Elixir’s track channels (1–7) at note 60, each voice at its own pitch. Its automation goes to the remote’s TRACKS channel: every per-track control has its name (volume, pan, filter, decay, pitch, the sends, density, mute, solo), pots 1–8 play the selected track’s strip, and program changes 1–32 launch patterns.' },
      { name: 'ELIXIR_GLOBAL.txt', title: 'Global', what: 'A second drum track on the remote’s GLOBAL channel. Its lanes are the performance buttons that act while held (fill, freeze, the throws, tape stop, the sweeps, reverse): a note’s length is the hold. Its automation names the tempo, morph, the effects, the delay and reverb, the macros and the rolls; pots on morph, the master filter, FX wet, reverb, density and macros 1–2.' },
    ],
    steps: (t, g) => [
      FM1(t, g),
      ['The SD card', 'copy the files into the HAPAX folder, next to the projects.'],
      ['On the Hapax', 'hold 2ND and press the track’s button; on INSTRUMENT DEFINITION, click one of the two lower right encoders and pick the file. Then set the track’s output port to the one the FM-1 is on (the files leave it as it is).'],
      ['Connecting', 'a Hapax MIDI out into the FM-1’s TRS MIDI input, or the Hapax’s USB host port. For MIDI clock, turn Elixir’s tempo down to MIDI.'],
    ],
    otherChannels: 'the files use 15 and 16: change OUTCHAN in them (a text editor) to match.',
    source: { href: 'https://squarp.net/hapax/manual/modetrack/', text: 'Squarp’s instrument definitions' },
  },
  {
    id: 'oxi', name: 'OXI One', folder: 'oxi',
    lead: 'OXI One instrument definitions (mk1 and mk2), written from the remote map with every build. A definition names an instrument’s CCs for the OXI’s modulation lanes and CC targets; the channel and the drum notes are set on the OXI.',
    files: [
      { name: 'ELIXIR_TRACKS.oxiindef', title: 'Tracks', what: 'The remote’s TRACKS channel: volume, pan, filter, resonance, decay, pitch, the delay, FX and reverb sends, sample rate, LFO, density, mute and solo, for each of D1 to OH and the selected track.' },
      { name: 'ELIXIR_GLOBAL.oxiindef', title: 'Global', what: 'The remote’s GLOBAL channel: tempo, shuffle, morph, the master filter, the FX bus, delay and reverb, density, build and vary, the macros, the performance effects, track select, the rolls and drills.' },
    ],
    steps: (t, g) => [
      FM1(t, g),
      ['In the OXI App', 'Instruments → Edit → Import → OXI instrument, pick the file, and send the instruments to the OXI One.'],
      ['On the OXI', `give a track the instrument and its channel: ${t} for Tracks, ${g} for Global. Its modulation lanes and CC targets then show the four-letter names.`],
      ['Playing the drums', 'a multitrack on Elixir’s track channels 1–7 at note 60 plays D1 to OH, each at its own pitch; or one channel, with a note per track set in CONFIG → MIDI NOTES.'],
      ['Connecting', 'an OXI MIDI out into the FM-1’s TRS MIDI input, or USB through a host. For MIDI clock, turn Elixir’s tempo down to MIDI.'],
    ],
    otherChannels: 'the definitions carry no channel: set the OXI track’s to match.',
    legend: 'The names: the track (D1 D2 D3 SN CY CH OH, SL the selected track), then the control: Vo volume, Pn pan, Fl filter, Rs resonance, Dc decay, Pt pitch, Dl delay send, Fx FX send, Rv reverb send, SR sample rate, Lf LFO, Dn density, Mu mute, So solo. On Global, RD1 is D1’s roll, DD1 its drill, FD1 its roll rate, SD1 selects it.',
    source: { href: 'https://midi.guide/blog/adding-oxi-instrument-definition-support/', text: 'MIDI Guide’s exports' },
  },
])
export const sequencerById = (id: string) => SEQUENCERS.find(s => s.id === id)
