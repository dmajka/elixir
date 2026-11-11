/** Controllers by family: the models of one family share a layout and control ids, so a template moves between them
 * (convertTemplate). Recognizing a model: its MIDI port names (detectModels), or its identity reply (identifyModel),
 * which only the models with a captured reply have. Neither authorizes a write: each uploader checks for itself. */
import type { ControllerTemplate, DeviceProfile } from '../core/mapping-engine/types'
import { XL3_PROFILE } from './novation/launch-control-xl3/profile'
import { LCXL_MK2_PROFILE } from './novation/launch-control-xl-mk2/profile'
import { LAUNCHPAD_PRO_MK3_PROFILE } from './novation/launchpad-pro-mk3/profile'

export interface DeviceFamily { id: string; name: string; models: readonly DeviceProfile[] }
export const FAMILIES: readonly DeviceFamily[] = Object.freeze([
  { id: 'launch-control-xl', name: 'Launch Control XL', models: [XL3_PROFILE, LCXL_MK2_PROFILE] },
  { id: 'launchpad', name: 'Launchpad', models: [LAUNCHPAD_PRO_MK3_PROFILE] },
])
export const ALL_PROFILES: readonly DeviceProfile[] = FAMILIES.flatMap(f => f.models)
export const profileById = (id: string) => ALL_PROFILES.find(p => p.id === id)
export const familyOf = (id: string) => FAMILIES.find(f => f.models.some(m => m.id === id))
/** the short name within its family: "XL 3", "XL mk2", "Pro MK3" */
export const modelName = (p: DeviceProfile) => p.model.replace(/^Launch Control |^Launchpad /, '')
/** the models this page sends templates to (each with its own checks: HardwareConnection.vue) */
export const canWrite = (p: DeviceProfile) => [XL3_PROFILE.id, LCXL_MK2_PROFILE.id, LAUNCHPAD_PRO_MK3_PROFILE.id].includes(p.id)

// The main port of each model, not its DAW, HUI, DIN or bootloader ports
const PORTS: [string, RegExp][] = [
  [XL3_PROFILE.id, /LCXL3|Launch Control XL ?(?:MK3|3)\b/i],
  [LCXL_MK2_PROFILE.id, /^(?!.*(?:LCXL3|XL ?(?:MK)?3)).*Launch Control XL/i],
  [LAUNCHPAD_PRO_MK3_PROFILE.id, /LPProMK3|Launchpad Pro MK3/i],
]
const NOT_MAIN = /DAW|HUI|DIN|Bootloader|MIDIIN|MIDIOUT|MIDI ?[23]\b/i
export function detectModels(portNames: readonly string[]): string[] {
  return PORTS.filter(([, re]) => portNames.some(n => re.test(n) && !NOT_MAIN.test(n))).map(([id]) => id)
}

/** The reply to the universal identity request (F0 7E 7F 06 01 F7): Novation (00 20 29) and a family code */
export const IDENTITY_REQUEST = Uint8Array.of(0xf0, 0x7e, 0x7f, 6, 1, 0xf7)
// (the mk2's: research/hardware/lcxl-mk2/identity.syx)
const FAMILY_CODES: [string, number, number][] = [[XL3_PROFILE.id, 0x48, 1], [LCXL_MK2_PROFILE.id, 0x61, 0], [LAUNCHPAD_PRO_MK3_PROFILE.id, 0x23, 1]]
export const isIdentityReply = (b: Uint8Array) => b.length >= 15 && b[0] === 0xf0 && b[1] === 0x7e && b[3] === 6 && b[4] === 2 && b[b.length - 1] === 0xf7
export function identifyModel(reply: Uint8Array): string | undefined {
  if (!isIdentityReply(reply) || reply[5] !== 0 || reply[6] !== 0x20 || reply[7] !== 0x29) return undefined
  return FAMILY_CODES.find(([, a, b]) => reply[8] === a && reply[9] === b)?.[0]
}

// Lights by name, for moving a template between models with different palettes (XL 3 velocities ↔ mk2 velocities)
const COLOR_NAMES: Record<number, string> = { 0: 'off', 5: 'red', 9: 'amber', 13: 'yellow', 17: 'lime', 21: 'green', 25: 'mint',
  12: 'off', 15: 'red', 63: 'amber', 62: 'yellow', 60: 'green' }
const base = (name?: string) => name === 'lime' || name === 'mint' ? 'green' : name
function mapColor(color: number, colors: readonly number[]) {
  const name = COLOR_NAMES[color]
  if (colors.includes(color)) return color
  if (!name) return undefined
  return colors.find(c => COLOR_NAMES[c] === name) ?? colors.find(c => base(COLOR_NAMES[c]) === base(name))
}
/** The template on another model of its family: what the model can't do is dropped, and said. */
export function convertTemplate(template: ControllerTemplate, to: DeviceProfile, messageTypeOf: (parameterId: string) => string | undefined): { template: ControllerTemplate; dropped: string[] } {
  const next = structuredClone(template), dropped: string[] = []
  next.deviceProfileId = to.id
  if (next.destinationSlot !== undefined && next.destinationSlot > to.customModeSlots) delete next.destinationSlot
  next.assignments = next.assignments.filter(a => {
    const c = to.controls.find(c => c.id === a.controlId)
    const type = a.messageType ?? messageTypeOf(a.parameterId)
    if (!c || !type || !c.messageTypes.includes(type as never)) { dropped.push(a.controlId); return false }
    if (a.encoderMode && !c.encoderModes.includes(a.encoderMode)) delete a.encoderMode
    if (a.buttonMode && !c.buttonModes.includes(a.buttonMode)) delete a.buttonMode
    if (a.colors) {
      const colors: { off?: number; on?: number } = {}
      for (const k of ['off', 'on'] as const) {
        const v = a.colors[k], mapped = v === undefined || !c.led[k] ? undefined : mapColor(v, c.led.colors)
        if (mapped !== undefined) colors[k] = mapped
      }
      if (Object.keys(colors).length) a.colors = colors; else delete a.colors
    }
    return true
  })
  return { template: next, dropped }
}
