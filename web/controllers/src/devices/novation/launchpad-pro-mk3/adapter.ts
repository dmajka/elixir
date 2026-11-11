import type { FileSerializer, MappingResult } from '../../../core/mapping-engine/types'
import { blankLaunchpadMode, encodeLaunchpadFile, type PadType } from './codec'
import { LAUNCHPAD_PRO_MK3_PROFILE } from './profile'
export const LAUNCHPAD_PRO_MK3_FILE_SERIALIZER: FileSerializer = {
  deviceProfileId: LAUNCHPAD_PRO_MK3_PROFILE.id, extension: 'syx',
  serialize(mapping: MappingResult) {
    if (!mapping.valid || mapping.profile.id !== LAUNCHPAD_PRO_MK3_PROFILE.id) throw new Error('A valid MK3 mapping is required')
    const mode = blankLaunchpadMode(mapping.template.name)
    for (const a of mapping.assignments) {
      const address = 10 * (8 - a.control.row) + a.control.column + 1
      const p = mode.pads.find(p => p.address === address)
      if (!p || a.control.kind !== 'pad' || !['cc', 'note', 'program-change'].includes(a.messageType) || !a.channel || a.encoderMode !== 'absolute' || a.colors?.on !== undefined) throw new Error('Unsupported MK3 assignment')
      Object.assign(p, { type: a.messageType as PadType, channel: a.channel, number: a.number,
        behavior: a.messageType === 'program-change' ? 'trigger' : a.buttonMode,
        onValue: a.messageType === 'cc' ? a.maximum : 0, offValue: a.messageType === 'cc' ? a.minimum : 0, color: a.colors?.off ?? 5 })
    }
    return encodeLaunchpadFile(mode)
  },
}
