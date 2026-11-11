import type { DeviceProfile, PhysicalControl } from '../../../core/mapping-engine/types'
const colors = Object.freeze([0, 3, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57])
const controls: PhysicalControl[] = Array.from({ length: 64 }, (_, i) => ({
  id: `pad.${Math.floor(i / 8) + 1}.${i % 8 + 1}`, name: `Pad ${Math.floor(i / 8) + 1}/${i % 8 + 1}`, kind: 'pad',
  group: `pads.${Math.floor(i / 8) + 1}`, row: Math.floor(i / 8), column: i % 8,
  messageTypes: ['cc', 'note', 'program-change'], encoderModes: [], buttonModes: ['momentary', 'toggle'],
  led: { off: true, on: false, colors },
}))
export const LAUNCHPAD_PRO_MK3_PROFILE: DeviceProfile = Object.freeze<DeviceProfile>({
  id: 'novation.launchpad-pro-mk3', manufacturer: 'Novation', model: 'Launchpad Pro MK3', generation: 'MK3',
  identity: { productBytes: [2, 14], portNameHints: ['LPProMK3', 'Launchpad Pro MK3'] },
  controls: Object.freeze(controls), customModeSlots: 8,
  text: { mode: { maximumLength: 32, ascii: true }, control: { maximumLength: 31, ascii: true } },
  protocol: { format: 'novation-launchpad-pro-mk3-binary-v1', fileExport: 'verified', hardwareUpload: 'verified' },
  firmware: { minimumBuild: 430, hardwareTested: true }, feedback: 'unverified', relativeValueFormat: 'unverified',
  limitations: ['Single-pad CC/note/program widgets on the 8×8 grid; surrounding navigation buttons are not assignable here.',
    'Fader/keyboard widgets and continuous macros are not implemented. Note pads play the tracks at their velocity on the tracks’ own channels (Drum Record).',
    'Pad labels are workspace metadata; hardware stores no per-pad text. Shared active color is green (21).',
    'Direct upload verified for slot1, firmware build483 and explicit-channel momentary CC, note and off pads (note pads: the Drums Square restore, read back exactly). Other slots/builds, program/toggle uploads, reconnect persistence and unrelated-slot preservation await hardware validation. External LED feedback is unverified.'],
})
