import type { MappingResult } from '../core/mapping-engine/types'
import { findParametersForMessage } from '../elixir/parameter-registry'

/** One complete Web MIDI event. Raw bytes remain available for unsupported/system messages. */
export function decodeMidi(bytes: readonly number[]) {
  const invalid = { type: 'invalid', channel: undefined, number: undefined, value: undefined }
  if (!bytes.length || bytes.some(b => !Number.isInteger(b) || b < 0 || b > 255)) return invalid
  const status = bytes[0]!, kind = status & 0xf0
  if (status === 0xf0) return { ...invalid, type: bytes.at(-1) === 0xf7 ? 'sysex' : 'invalid' }
  if (status >= 0xf8 && bytes.length === 1) return { ...invalid, type: 'realtime', number: status }
  const length = kind === 0xc0 || kind === 0xd0 ? 2 : 3
  if (status < 0x80 || status >= 0xf0 || bytes.length !== length || bytes.slice(1).some(b => b > 127)) return invalid
  return { type: kind === 0xb0 ? 'cc' : kind === 0x80 || kind === 0x90 ? 'note' : kind === 0xc0 ? 'program-change' : 'other',
    channel: (status & 15) + 1, number: bytes[1], value: kind === 0xc0 ? undefined : kind === 0x80 ? 0 : bytes[2] }
}
export function inspectMidi(bytes: readonly number[], mapping: MappingResult, expectedControlId?: string) {
  const message = decodeMidi(bytes)
  const candidates = mapping.assignments.filter(a => a.messageType === message.type && a.channel === message.channel && a.number === message.number)
  const expected = expectedControlId ? mapping.assignments.find(a => a.control.id === expectedControlId) : undefined
  const inRange = (a: typeof mapping.assignments[number]) => message.value === undefined || (message.value >= a.minimum && message.value <= a.maximum)
  const status = expectedControlId
    ? !expected ? 'unassigned' : !candidates.includes(expected) ? 'address mismatch' : !inRange(expected) ? 'value outside range' : 'match'
    : !candidates.length ? 'unmapped' : candidates.length > 1 ? 'ambiguous' : !inRange(candidates[0]!) ? 'value outside range' : 'address match'
  const parameters = ['cc', 'note', 'program-change', 'realtime'].includes(message.type)
    ? findParametersForMessage(message.type as 'cc' | 'note' | 'program-change' | 'realtime', message.number!, message.channel,
      { remoteTracks: mapping.template.channels.remoteTracks!, remoteGlobal: mapping.template.channels.remoteGlobal! }).map(p => p.name) : []
  return { ...message, status, parameters, controls: candidates.map(a => a.control.id) }
}
/** Offline only. No transport dependency or MIDI output. State belongs to this mapping snapshot. */
export function createControllerSimulator(mapping: MappingResult) {
  if (!mapping.valid) throw new Error('A valid mapping is required')
  const assignments = structuredClone(mapping.assignments), toggles = new Map<string, boolean>()
  return {
    emit(controlId: string, action: 'value' | 'press' | 'release', value = 0): number[] {
      const a = assignments.find(a => a.control.id === controlId)
      if (!a) throw new Error('Control is unassigned')
      if (a.encoderMode === 'relative') throw new Error('Relative value format is unverified; simulation unavailable')
      if (a.channel === undefined || !['cc', 'note', 'program-change'].includes(a.messageType)) throw new Error('Unsupported simulation message')
      const button = ['button', 'pad'].includes(a.control.kind)
      if (button === (action === 'value')) throw new Error('Choose an action appropriate to the physical control')
      if (a.messageType === 'program-change') return action === 'press' ? [0xc0 | (a.channel - 1), a.number] : []
      if (button && a.buttonMode === 'toggle') {
        if (action === 'release') return []
        toggles.set(controlId, !toggles.get(controlId)); value = toggles.get(controlId) ? a.maximum : a.minimum
      } else if (button) value = action === 'press' ? a.maximum : a.minimum
      if (!Number.isInteger(value) || value < a.minimum || value > a.maximum) throw new Error(`Value must be ${a.minimum}–${a.maximum}`)
      return [(a.messageType === 'note' ? 0x90 : 0xb0) | (a.channel - 1), a.number, value]
    },
  }
}
