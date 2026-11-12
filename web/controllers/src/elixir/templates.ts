/** Semantic XL3 presets. No UI or MIDI I/O; generation always returns editable, detached data. */
import type { ControllerTemplate, MidiAssignment } from '../core/mapping-engine/types'
import { createElixirMappingEngine, createEmptyTemplate } from './mapping-context'
import { DEFAULT_CHANNEL_SETTINGS, type ChannelSettings } from './channel-settings'
import { TRACKS } from './parameter-registry'

export const TEMPLATE_CATALOG = Object.freeze([
  Object.freeze({ id: 'mixer', name: 'Elixir Mixer', description: 'Eight track columns: volume, sends, mute and solo.' }),
  Object.freeze({ id: 'performance', name: 'Elixir Perform', description: 'Density, morph, macros, roll rates and held performance actions.' }),
  Object.freeze({ id: 'effects', name: 'Elixir Effects', description: 'Delay, reverb, FX bus, routing and track FX sends.' }),
  Object.freeze({ id: 'sound-editor', name: 'Elixir Sound', description: 'Eight filters and resonances; pitch, decay, sample rate and pan for D1, D2, D3 and selected track.' }),
  Object.freeze({ id: 'sequencer', name: 'Elixir Sequencer', description: 'Density, roll, track selection, pattern navigation and pattern 1–4 triggers.' }),
] as const)
export type BuiltinTemplateId = typeof TEMPLATE_CATALOG[number]['id']
const engine = createElixirMappingEngine()
const trackRow = (key: string) => TRACKS.map(track => `tracks.${track}.${key}`)
const global = (...keys: string[]) => keys.map(key => `global.${key}`)
const soundTracks = ['d1', 'd2', 'd3', 'selected']
const soundRow = (key: string) => soundTracks.map(track => `tracks.${track}.${key}`)

export function generateTemplate(id: BuiltinTemplateId, channels: ChannelSettings = DEFAULT_CHANNEL_SETTINGS): ControllerTemplate {
  const preset = TEMPLATE_CATALOG.find(p => p.id === id)
  if (!preset) throw new Error(`Unknown template: ${id}`)
  const template = createEmptyTemplate(`elixir.${id}`, preset.name)
  template.channels = { ...channels }
  function row(group: string, ids: readonly string[]) {
    ids.forEach((parameterId, index) => template.assignments.push({ controlId: `${group}.${index + 1}`, parameterId }))
  }
  const muteSolo = () => { row('button.1', trackRow('mute')); row('button.2', trackRow('solo')) }
  if (id === 'mixer') {
    row('encoder.1', trackRow('reverb-send')); row('encoder.2', trackRow('delay-send')); row('encoder.3', trackRow('fx-send'))
    row('fader', trackRow('volume')); muteSolo()
  } else if (id === 'sound-editor') {
    row('encoder.1', trackRow('filter-frequency')); row('encoder.2', trackRow('resonance'))
    row('encoder.3', [...soundRow('pitch'), ...soundRow('decay')])
    row('fader', [...soundRow('sample-rate'), ...soundRow('pan')]); muteSolo()
  } else if (id === 'performance') {
    row('encoder.1', global('density', 'morph', 'macro-1', 'macro-2', 'macro-3', 'macro-4', 'roll-rate', 'master-filter'))
    row('encoder.2', global('build', 'vary', 'drill', 'jitter', 'cuts', 'pump', 'repeat', 'filter-resonance'))
    row('encoder.3', trackRow('density')); row('fader', TRACKS.map(t => `global.roll-fader.${t}`))
    row('button.1', global('fill', 'repeat-held', 'freeze', 'throw-reverb', 'throw-fx', 'feedback-push', 'tape-stop', 'reverse'))
    row('button.2', global(...['half', 'jitter', 'cuts', 'short', 'pump', 'comb', 'space', 'crush'].map(k => `effect.${k}`)))
  } else if (id === 'effects') {
    row('encoder.1', global(...['time', 'feedback', 'tone', 'band', 'wow', 'glitch', 'route', 'reverb-to-fx'].map(k => `delay.${k}`)))
    row('encoder.2', global('reverb-level', 'reverb-size', 'reverb-damping', 'echo-to-reverb', 'fx-wet', 'bus-dirt', 'saturate', 'master-dirt'))
    row('encoder.3', global('fx-p1', 'fx-p2', 'fx-p3', 'fx-p4', 'comb-feedback', 'ring.wet', 'ring.tone', 'ring.spread'))
    row('fader', trackRow('fx-send'))
    row('button.1', global('freeze', 'throw-reverb', 'throw-fx', 'feedback-push', 'tape-stop', 'low-pass-sweep', 'high-pass-sweep', 'reverse'))
    row('button.2', global(...['half', 'jitter', 'cuts', 'short', 'pump', 'comb', 'space', 'crush'].map(k => `effect.${k}`)))
  } else {
    row('encoder.1', trackRow('density'))
    row('encoder.2', global('tempo', 'shuffle', 'density', 'ghost-velocity', 'build', 'vary', 'roll-rate', 'repeat'))
    row('encoder.3', trackRow('volume')); row('fader', TRACKS.map(t => `global.roll-fader.${t}`))
    row('button.1', [...global(...TRACKS.slice(0, 7).map(t => `select.${t}`)), 'global.control-all-revert'])
    row('button.2', global('pattern-previous', 'pattern-next', 'fill', 'repeat-held', 'pattern.1', 'pattern.2', 'pattern.3', 'pattern.4'))
  }
  // Reject unknown channels/unsupported assignments at generation, preserving overlap warnings for inspection.
  engine.resolve(template)
  return template
}

/** Create partial or complete user templates; omitted controls are reported and exported disabled. */
export function createUserTemplate(id: string, name: string, assignments: readonly MidiAssignment[] = [], channels: ChannelSettings = DEFAULT_CHANNEL_SETTINGS): ControllerTemplate {
  const template = createEmptyTemplate(id, name)
  template.channels = { ...channels }
  template.assignments = structuredClone([...assignments])
  engine.resolve(template)
  return template
}

/** Validate edits atomically; invalid replacements never modify the source template. */
export function editTemplateAssignment(template: ControllerTemplate, controlId: string, assignment: Omit<MidiAssignment, 'controlId'> | null): ControllerTemplate {
  if (!engine.resolve(template).profile.controls.some(c => c.id === controlId)) throw new Error(`Unknown control: ${controlId}`)
  const edited = structuredClone(template)
  edited.assignments = edited.assignments.filter(a => a.controlId !== controlId)
  if (assignment) edited.assignments.push({ ...structuredClone(assignment), controlId })
  engine.resolve(edited)
  return edited
}
