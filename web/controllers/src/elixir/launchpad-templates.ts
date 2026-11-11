/** MK3 single-pad presets; no continuous-on-button mappings. Drum record plays the tracks from note pads on their
 * own channels (tracks.d1.hit ..: note 60, the pad's velocity), recorded under REC as any MIDI note in is. */
import type { ChannelSettings } from './channel-settings'
import { DEFAULT_CHANNEL_SETTINGS, withTrackChannels } from './channel-settings'
import { createElixirMappingEngine, createEmptyTemplate } from './mapping-context'
import { LAUNCHPAD_PRO_MK3_PROFILE } from '../devices/novation/launchpad-pro-mk3/profile'
import { TRACKS } from './parameter-registry'
export const LAUNCHPAD_TEMPLATE_CATALOG = Object.freeze([
  { id: 'pad-mute-solo', name: 'Elixir Mute Solo', description: 'Top two rows: eight track mutes and solos. Other pads remain unassigned.' },
  { id: 'pad-patterns', name: 'Elixir Patterns', description: 'Patterns 1–32 across the top four rows on Remote Global; remaining pads are unassigned.' },
  { id: 'pad-performance', name: 'Elixir Pad Perform', description: 'Held effects, fill, throws and track selection; remaining pads are unassigned.' },
  { id: 'pad-drum-record', name: 'Elixir Drum Record', description: 'Seven velocity pads in a square at the bottom left (D1–D3, SN, CH, OH, CY on their own channels), Record and Play at the bottom right, performance rows on top.' },
] as const)
export type LaunchpadTemplateId = typeof LAUNCHPAD_TEMPLATE_CATALOG[number]['id']
export function generateLaunchpadTemplate(id: LaunchpadTemplateId, channels: ChannelSettings = DEFAULT_CHANNEL_SETTINGS) {
  const preset = LAUNCHPAD_TEMPLATE_CATALOG.find(p => p.id === id)
  if (!preset) throw new Error(`Unknown MK3 template ${id}`)
  const t = createEmptyTemplate(`elixir.${id}`, preset.name, LAUNCHPAD_PRO_MK3_PROFILE.id)
  t.channels = { ...channels }
  const row = (r: number, ids: string[]) => ids.forEach((parameterId, i) => t.assignments.push({ controlId: `pad.${r}.${i + 1}`, parameterId }))
  if (id === 'pad-drum-record') return drumRecord(t)
  if (id === 'pad-mute-solo') {
    row(1, TRACKS.map(track => `tracks.${track}.mute`)); row(2, TRACKS.map(track => `tracks.${track}.solo`))
  } else if (id === 'pad-patterns') {
    for (let r = 1; r <= 4; r++) row(r, Array.from({ length: 8 }, (_, i) => `global.pattern.${(r - 1) * 8 + i + 1}`))
  } else {
    row(1, ['fill', 'repeat-held', 'freeze', 'throw-reverb', 'throw-fx', 'feedback-push', 'tape-stop', 'reverse'].map(k => `global.${k}`))
    row(2, ['half', 'jitter', 'cuts', 'short', 'pump', 'comb', 'space', 'crush'].map(k => `global.effect.${k}`))
    row(3, [...TRACKS.slice(0, 7).map(k => `global.select.${k}`), 'global.control-all-revert'])
    row(4, ['pattern-previous', 'pattern-next'].map(k => `global.${k}`))
  }
  createElixirMappingEngine().resolve(t)
  return t
}
// Drum record: rows 1–5 performance (held effects, latching effects, rolls, drills, mutes), the square at the bottom
// left (kicks along the bottom, snare and hats above, cymbal on top), transport at the bottom right.
type Pad = [string, number]
const DRUM_ROWS: (Pad | null)[][] = [
  ['fill', 'repeat-held', 'freeze', 'throw-reverb', 'throw-fx', 'feedback-push', 'tape-stop', 'reverse'].map(k => [`global.${k}`, 9]),
  ['half', 'jitter', 'cuts', 'short', 'pump', 'comb', 'space', 'crush'].map(k => [`global.effect.${k}`, 45]),
  [...TRACKS.slice(0, 7).map((k): Pad => [`global.roll.${k}`, 33]), ['global.low-pass-sweep', 41]],
  [...TRACKS.slice(0, 7).map((k): Pad => [`global.drill.${k}`, 29]), ['global.high-pass-sweep', 41]],
  [...TRACKS.slice(0, 7).map((k): Pad => [`tracks.${k}.mute`, 13]), null],
  [null, ['tracks.cy.hit', 57], null, null, null, null, null, null],
  [['tracks.sn.hit', 3], ['tracks.ch.hit', 21], ['tracks.oh.hit', 25], null, null, null, ['global.pattern-previous', 37], ['global.pattern-next', 37]],
  [['tracks.d1.hit', 5], ['tracks.d2.hit', 5], ['tracks.d3.hit', 5], null, null, null, ['global.record', 5], ['global.play-stop', 21]],
]
function drumRecord(t: ReturnType<typeof createEmptyTemplate>) {
  t.channels = withTrackChannels(t.channels)
  DRUM_ROWS.forEach((row, r) => row.forEach((pad, c) => {
    if (pad) t.assignments.push({ controlId: `pad.${r + 1}.${c + 1}`, parameterId: pad[0], colors: { off: pad[1] } })
  }))
  createElixirMappingEngine().resolve(t)
  return t
}
