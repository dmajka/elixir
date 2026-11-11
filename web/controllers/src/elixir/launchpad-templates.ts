/** MK3 single-pad presets; no invented voice triggers or continuous-on-button mappings. */
import type { ChannelSettings } from './channel-settings'
import { DEFAULT_CHANNEL_SETTINGS } from './channel-settings'
import { createElixirMappingEngine, createEmptyTemplate } from './mapping-context'
import { LAUNCHPAD_PRO_MK3_PROFILE } from '../devices/novation/launchpad-pro-mk3/profile'
import { TRACKS } from './parameter-registry'
export const LAUNCHPAD_TEMPLATE_CATALOG = Object.freeze([
  { id: 'pad-mute-solo', name: 'Elixir Mute Solo', description: 'Top two rows: eight track mutes and solos. Other pads remain unassigned.' },
  { id: 'pad-patterns', name: 'Elixir Patterns', description: 'Patterns 1–32 across the top four rows on Remote Global; remaining pads are unassigned.' },
  { id: 'pad-performance', name: 'Elixir Pad Perform', description: 'Held effects, fill, throws and track selection; remaining pads are unassigned.' },
] as const)
export type LaunchpadTemplateId = typeof LAUNCHPAD_TEMPLATE_CATALOG[number]['id']
export function generateLaunchpadTemplate(id: LaunchpadTemplateId, channels: ChannelSettings = DEFAULT_CHANNEL_SETTINGS) {
  const preset = LAUNCHPAD_TEMPLATE_CATALOG.find(p => p.id === id)
  if (!preset) throw new Error(`Unknown MK3 template ${id}`)
  const t = createEmptyTemplate(`elixir.${id}`, preset.name, LAUNCHPAD_PRO_MK3_PROFILE.id)
  t.channels = { ...channels }
  const row = (r: number, ids: string[]) => ids.forEach((parameterId, i) => t.assignments.push({ controlId: `pad.${r}.${i + 1}`, parameterId }))
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
