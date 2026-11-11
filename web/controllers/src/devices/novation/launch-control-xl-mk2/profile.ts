import type { DeviceProfile, PhysicalControl } from '../../../core/mapping-engine/types'
/** Launch Control XL (mk2): the XL 3's layout, with the same control ids, so a template moves between the two.
 * Its knobs are potentiometers (absolute only). Its user template file (codec.ts) is decoded from Components' exports;
 * Components offers no light colors for it, so none here. Checked on the user's mk2 (2026-11-11): the Mixer's file,
 * imported and sent with Components, passed the guided check. Sent from the page by Mk2Send.vue (identity first): tried on the user's mk2, 2026-11-11. */
const controls: PhysicalControl[] = []
for (let row = 1; row <= 3; row++) for (let col = 1; col <= 8; col++) controls.push({
  id: `encoder.${row}.${col}`, name: `Knob ${row}/${col}`, kind: 'encoder', group: `encoders.${row}`, row: row - 1, column: col - 1,
  messageTypes: ['cc'], encoderModes: ['absolute'], buttonModes: [], led: { off: false, on: false, colors: [] },
})
for (let col = 1; col <= 8; col++) controls.push({ id: `fader.${col}`, name: `Fader ${col}`, kind: 'fader', group: 'faders', row: 3, column: col - 1,
  messageTypes: ['cc'], encoderModes: ['absolute'], buttonModes: [], led: { off: false, on: false, colors: [] },
})
for (let row = 1; row <= 2; row++) for (let col = 1; col <= 8; col++) controls.push({
  id: `button.${row}.${col}`, name: `Button ${row}/${col}`, kind: 'button', group: `buttons.${row}`, row: row + 3, column: col - 1,
  messageTypes: ['cc', 'note'], encoderModes: [], buttonModes: ['momentary', 'toggle'], led: { off: false, on: false, colors: [] },
})
export const LCXL_MK2_PROFILE: DeviceProfile = Object.freeze({
  id: 'novation.launch-control-xl-mk2', manufacturer: 'Novation', model: 'Launch Control XL mk2', generation: 'mk2',
  // (its identity reply: family 61 00, research/hardware/lcxl-mk2/identity.syx)
  identity: Object.freeze({ productBytes: Object.freeze([]), portNameHints: Object.freeze(['Launch Control XL']) }),
  controls: Object.freeze(controls.map(c => Object.freeze({ ...c, messageTypes: Object.freeze(c.messageTypes), encoderModes: Object.freeze(c.encoderModes), buttonModes: Object.freeze(c.buttonModes), led: Object.freeze(c.led) }))),
  customModeSlots: 8, text: Object.freeze({ mode: Object.freeze({ maximumLength: 16, ascii: true }), control: Object.freeze({ maximumLength: 31, ascii: true }) }),
  protocol: Object.freeze({ format: 'novation-lcxl-mk2-components-v1', fileExport: 'verified', hardwareUpload: 'verified' }),
  firmware: Object.freeze({ hardwareTested: true }),
  feedback: 'unverified', relativeValueFormat: 'unverified',
  limitations: Object.freeze(['It cannot send a template back: the one replaced is not kept.',
    'Knobs have end stops (absolute only): use PICKUP on Elixir.', 'Buttons send CC or note; program changes (patterns) need another controller.',
    'No light colors: Components sets them.']),
})
