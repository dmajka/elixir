import type { ControllerTemplate, DeviceProfile } from '../core/mapping-engine/types'
import type { Parameter } from './parameter-registry'
const tracks = ['d1', 'd2', 'd3', 'sn', 'cy', 'ch', 'oh']
/** Build a detached row edit. Caller validates the complete candidate before committing it. */
export function assignTrackRow(template: ControllerTemplate, profile: DeviceProfile, registry: readonly Parameter[], controlId: string, parameterId: string): ControllerTemplate {
  const control = profile.controls.find(c => c.id === controlId)
  const parameter = registry.find(p => p.id === parameterId)
  const match = parameter?.id.match(/^tracks\.(?:d1|d2|d3|sn|cy|ch|oh|selected)\.(.+)$/)
  if (!control || !match) throw new Error('Choose a per-track parameter to apply across tracks 1–7.')
  const row = profile.controls.filter(c => c.row === control.row).sort((a, b) => a.column - b.column)
  if (row.length !== 8) throw new Error('This control does not have an eight-column row.')
  const next = JSON.parse(JSON.stringify(template)) as ControllerTemplate
  next.assignments = next.assignments.filter(a => !row.some(c => c.id === a.controlId))
  tracks.forEach((track, i) => {
    const id = `tracks.${track}.${match[1]}`
    if (!registry.some(p => p.id === id)) throw new Error(`Missing track parameter ${id}`)
    next.assignments.push({ controlId: row[i]!.id, parameterId: id })
  })
  // Elixir has no master mute/solo or generic master equivalent for each track effect.
  // Continuous rows reserve the eighth control for the documented master volume.
  if (['encoder', 'fader'].includes(control.kind)) next.assignments.push({ controlId: row[7]!.id, parameterId: 'tracks.master-volume' })
  return next
}
