import type { DeviceProfile, PhysicalControl } from '../../../core/mapping-engine/types'
// Conservative common palette supported by encoder and button editor; not all button colors exposed yet.
const colors = Object.freeze([0, 3, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57])
const controls: PhysicalControl[] = []
for (let row = 1; row <= 3; row++) for (let col = 1; col <= 8; col++) controls.push({
  id: `encoder.${row}.${col}`, name: `Encoder ${row}/${col}`, kind: 'encoder', group: `encoders.${row}`, row: row - 1, column: col - 1,
  messageTypes: ['cc'], encoderModes: ['absolute', 'relative'], buttonModes: [], led: { off: true, on: false, colors },
})
for (let col = 1; col <= 8; col++) controls.push({ id: `fader.${col}`, name: `Fader ${col}`, kind: 'fader', group: 'faders', row: 3, column: col - 1,
  messageTypes: ['cc'], encoderModes: ['absolute'], buttonModes: [], led: { off: false, on: false, colors: [] },
})
for (let row = 1; row <= 2; row++) for (let col = 1; col <= 8; col++) controls.push({
  id: `button.${row}.${col}`, name: `Button ${row}/${col}`, kind: 'button', group: `buttons.${row}`, row: row + 3, column: col - 1,
  messageTypes: ['cc', 'note', 'program-change'], encoderModes: [], buttonModes: ['momentary', 'toggle'], led: { off: true, on: true, colors },
})
export const XL3_PROFILE: DeviceProfile = Object.freeze({
  id: 'novation.launch-control-xl3', manufacturer: 'Novation', model: 'Launch Control XL 3', generation: '3',
  identity: Object.freeze({ productBytes: Object.freeze([2, 21]), portNameHints: Object.freeze(['Launch Control XL MK3', 'LCXL3']) }),
  controls: Object.freeze(controls.map(c => Object.freeze({ ...c, messageTypes: Object.freeze(c.messageTypes), encoderModes: Object.freeze(c.encoderModes), buttonModes: Object.freeze(c.buttonModes), led: Object.freeze(c.led) }))),
  customModeSlots: 15, text: Object.freeze({ mode: Object.freeze({ maximumLength: 16, ascii: true }), control: Object.freeze({ maximumLength: 31, ascii: true }) }),
  protocol: Object.freeze({ format: 'novation-xl3-two-surface-v0', fileExport: 'verified', hardwareUpload: 'verified' }),
  firmware: Object.freeze({ minimumBuild: 1563, relativeEncoderBuild: 1563, hardwareTested: true }),
  feedback: 'unverified', relativeValueFormat: 'unverified',
  limitations: Object.freeze(['File-codec subset: CC7, button note/program; no NRPN, CC14 or HID yet.', 'Direct upload verified only on firmware build1563, slot1 and absolute CC mappings; other slots/firmware/messages are blocked. LED feedback remains unverified.', 'Port-name hints alone do not identify hardware safely.']),
})
