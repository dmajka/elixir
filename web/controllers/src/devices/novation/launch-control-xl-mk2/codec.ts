/** Launch Control XL mk2 user template, as Novation Components exports it (and, it seems, sends it). File only; no
 * MIDI I/O. Evidence: research/hardware/lcxl-mk2 (README.md): a new template (a), and one change each: knob 1/1 on
 * channel 9 (c), button 1/1 toggle (e2), button 1/1 on channel 9 (f).
 *
 *   F0 00 20 29 02 11 77 tt     77: a template; tt: user template 0-7
 *   56 controls x 6 bytes       24 knobs, 8 faders:  number, 0, min, max, channel-1, 0
 *                               16 buttons, then Device/Mute/Solo/Arm, then the four arrows:
 *                                                    type (0 note, 1 CC), number, off, on, toggle, channel-1
 *   24 x 3 bytes                Components' own (lights, it seems; it offers no color): kept as in (a)
 *   F7
 */
export interface Mk2Continuous { number: number; min: number; max: number; channel: number }
export interface Mk2Button { type: 'note' | 'cc'; number: number; off: number; on: number; toggle: boolean; channel: number }
export interface Mk2Template { slot: number; continuous: Mk2Continuous[]; buttons: Mk2Button[] }

const HEADER = [0xf0, 0, 0x20, 0x29, 2, 0x11, 0x77]
export const MK2_FILE_LENGTH = 8 + 56 * 6 + 24 * 3 + 1
// (a)'s last section: red, red, yellow, yellow, green, green, orange, orange, on each knob row
const TAIL = Array.from({ length: 3 }, () => [0x0f, 0x0f, 0x3e, 0x3e, 0x3c, 0x3c, 0x2f, 0x2f]).flat().flatMap(v => [0, v, 0])
const range = (from: number, n: number) => Array.from({ length: n }, (_, i) => from + i)
/** A new template as Components makes it: (a) */
export function defaultMk2Template(slot = 0): Mk2Template {
  const continuous = [...range(13, 8), ...range(29, 8), ...range(49, 8), ...range(77, 8)].map(number => ({ number, min: 0, max: 127, channel: 1 }))
  const notes = [...range(41, 4), ...range(57, 4), ...range(73, 4), ...range(89, 4), ...range(105, 4)]
  const buttons: Mk2Button[] = [...notes.map(number => ({ type: 'note' as const, number, off: 0, on: 127, toggle: false, channel: 1 })),
    ...range(104, 4).map(number => ({ type: 'cc' as const, number, off: 0, on: 127, toggle: false, channel: 1 }))]
  return { slot, continuous, buttons }
}
function byte(value: number, min: number, max: number, field: string) {
  if (!Number.isInteger(value) || value < min || value > max) throw new Error(`${field} must be ${min}–${max}`)
  return value
}
export function encodeMk2Template(t: Mk2Template): Uint8Array {
  if (t.continuous.length !== 32 || t.buttons.length !== 24) throw new Error('An mk2 template has 32 knobs and faders and 24 buttons')
  const out = [...HEADER, byte(t.slot, 0, 7, 'User template')]
  t.continuous.forEach((c, i) => out.push(byte(c.number, 0, 127, `Control ${i + 1} CC`), 0, byte(c.min, 0, 127, 'Minimum'), byte(c.max, 0, 127, 'Maximum'), byte(c.channel, 1, 16, 'Channel') - 1, 0))
  t.buttons.forEach((b, i) => out.push(b.type === 'cc' ? 1 : 0, byte(b.number, 0, 127, `Button ${i + 1} number`), byte(b.off, 0, 127, 'Off value'), byte(b.on, 0, 127, 'On value'), b.toggle ? 1 : 0, byte(b.channel, 1, 16, 'Channel') - 1))
  out.push(...TAIL, 0xf7)
  return Uint8Array.from(out)
}
/** Reads what encodeMk2Template writes (and Components' exports): the fields above; the last section is not read. */
export function decodeMk2Template(data: Uint8Array): Mk2Template {
  if (data.length !== MK2_FILE_LENGTH || !HEADER.every((b, i) => data[i] === b) || data.at(-1) !== 0xf7 || data.slice(1, -1).some(b => b > 127)) throw new Error('Not a Launch Control XL mk2 template file')
  const at = (i: number) => data.slice(8 + i * 6, 14 + i * 6)
  const continuous = range(0, 32).map(i => { const e = at(i); return { number: e[0]!, min: e[2]!, max: e[3]!, channel: e[4]! + 1 } })
  const buttons = range(32, 24).map(i => {
    const e = at(i)
    if (e[0]! > 1 || e[4]! > 1) throw new Error(`Unknown button setting in control ${i + 1}`)
    return { type: e[0] === 1 ? 'cc' as const : 'note' as const, number: e[1]!, off: e[2]!, on: e[3]!, toggle: e[4] === 1, channel: e[5]! + 1 }
  })
  return { slot: data[7]!, continuous, buttons }
}
