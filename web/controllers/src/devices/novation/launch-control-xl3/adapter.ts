import type { FileSerializer, MappingResult } from '../../../core/mapping-engine/types'
import { createDefaultMode, encodeFile } from './codec'
import { XL3_PROFILE } from './profile'
// Explicit protocol addressing stays inside this adapter, not the semantic engine.
const wireIds = new Map(XL3_PROFILE.controls.map((c, i) => [c.id, 16 + i]))
export function serializeXL3Mapping(mapping: MappingResult): Uint8Array {
  if (!mapping.valid || mapping.profile.id !== XL3_PROFILE.id) throw new Error('Expected a validated XL3 mapping')
  const mode = createDefaultMode(mapping.template.name)
  for (const c of mode.controls) { c.type = 'off'; c.name = '' }
  for (const a of mapping.assignments) {
    const wireId = wireIds.get(a.control.id)
    if (wireId === undefined || a.channel === undefined) throw new Error('Unknown XL3 control or missing channel')
    const c = mode.controls[wireId - 16]!
    if (!['cc', 'note', 'program-change'].includes(a.messageType)) throw new Error('Unsupported XL3 message type')
    Object.assign(c, { type: a.messageType === 'program-change' ? 'program' : a.messageType, channel: a.channel, number: a.number,
      name: a.label, min: a.minimum, max: a.maximum, relative: a.encoderMode === 'relative', behavior: a.messageType === 'program-change' ? 'trigger' : a.buttonMode,
      ...(a.colors?.off !== undefined ? { offColor: a.colors.off } : {}), ...(a.colors?.on !== undefined ? { onColor: a.colors.on } : {}) })
  }
  return encodeFile(mode) // Always destination127. No hardware upload or slot activation.
}
export const XL3_FILE_SERIALIZER: FileSerializer = Object.freeze({ deviceProfileId: XL3_PROFILE.id, extension: 'syx', serialize: serializeXL3Mapping })
