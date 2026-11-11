import type { FileSerializer, MappingResult } from '../../../core/mapping-engine/types'
import { defaultMk2Template, encodeMk2Template } from './codec'
import { LCXL_MK2_PROFILE } from './profile'
/** The mapping as a user template file for Components. Controls without an assignment keep Components' defaults
 * (channel 1, which Elixir's remote channels aren't, unless set so). */
export function serializeMk2Mapping(mapping: MappingResult): Uint8Array {
  if (!mapping.valid || mapping.profile.id !== LCXL_MK2_PROFILE.id) throw new Error('Expected a validated Launch Control XL mk2 mapping')
  const t = defaultMk2Template((mapping.template.destinationSlot ?? 1) - 1)
  for (const a of mapping.assignments) {
    if (a.channel === undefined) throw new Error('Missing channel')
    const [kind, x, y] = a.control.id.split('.'), row = Number(x), col = Number(y)
    if (kind === 'encoder') Object.assign(t.continuous[(row - 1) * 8 + col - 1]!, { number: a.number, min: a.minimum, max: a.maximum, channel: a.channel })
    else if (kind === 'fader') Object.assign(t.continuous[24 + row - 1]!, { number: a.number, min: a.minimum, max: a.maximum, channel: a.channel })
    else if (kind === 'button' && (a.messageType === 'cc' || a.messageType === 'note')) Object.assign(t.buttons[(row - 1) * 8 + col - 1]!, { type: a.messageType, number: a.number, off: a.minimum, on: a.maximum, toggle: a.buttonMode === 'toggle', channel: a.channel })
    else throw new Error(`Unsupported mk2 assignment on ${a.control.id}`)
  }
  return encodeMk2Template(t)
}
export const LCXL_MK2_FILE_SERIALIZER: FileSerializer = Object.freeze({ deviceProfileId: LCXL_MK2_PROFILE.id, extension: 'syx', serialize: serializeMk2Mapping })
