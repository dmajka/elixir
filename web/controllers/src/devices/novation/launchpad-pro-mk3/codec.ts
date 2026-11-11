/** Independent MK3 version-1 file codec. Single-pad widgets only; no hardware writes. */
export type PadType = 'off' | 'note' | 'cc' | 'program-change'
export interface LaunchpadPad { address: number; channel: number | 'global'; type: PadType; number: number; behavior: 'momentary' | 'toggle' | 'trigger'; onValue: number; offValue: number; color: number }
export interface LaunchpadMode { name: string; onColor: number; pads: LaunchpadPad[] }
const header = [0xf0, 0, 0x20, 0x29, 2, 14, 5, 1, 127]
const types: PadType[] = ['off', 'note', 'cc', 'program-change']
export const PAD_ADDRESSES = Object.freeze(Array.from({ length: 64 }, (_, i) => 10 * (8 - Math.floor(i / 8)) + i % 8 + 1))
function byte(n: number) { if (!Number.isInteger(n) || n < 0 || n > 127) throw new Error('Expected a 7-bit integer'); return n }
export function blankLaunchpadMode(name = '*New Custom Mode'): LaunchpadMode {
  return { name, onColor: 21, pads: PAD_ADDRESSES.map(address => ({ address, channel: 'global', type: 'off', number: 0, behavior: 'momentary', onValue: 0, offValue: 0, color: 0 })) }
}
export function encodeLaunchpadFile(mode: LaunchpadMode): Uint8Array {
  if (!/^[\x20-\x7d]{0,32}$/.test(mode.name)) throw new Error('MK3 name must be at most 32 printable ASCII characters')
  if (mode.pads.length !== 64 || new Set(mode.pads.map(p => p.address)).size !== 64) throw new Error('Expected exactly 64 unique pads')
  const records: number[] = []
  for (const address of PAD_ADDRESSES) {
    const p = mode.pads.find(p => p.address === address)
    if (!p || !types.includes(p.type)) throw new Error('Unknown pad address/type')
    const channel = p.channel === 'global' ? 127 : p.channel - 1
    if (channel !== 127 && (!Number.isInteger(channel) || channel < 0 || channel > 15)) throw new Error('Channel must be 1–16 or global')
    const behavior = ['momentary', 'toggle', 'trigger'].indexOf(p.behavior)
    if (behavior < 0 || (p.type === 'note' && behavior === 2) || (p.type === 'program-change' && behavior !== 2)) throw new Error('Unsupported pad behavior')
    records.push(address, channel, types.indexOf(p.type), byte(p.number), behavior, byte(p.onValue), byte(p.offValue), byte(p.color))
  }
  // Version-0 metadata (empty), delimiter, default palette/lightshow flags, shared active color.
  return Uint8Array.from([...header, ...Array.from(mode.name, c => c.charCodeAt(0)), 0, 0, 127, 0, 0, byte(mode.onColor), ...records, 0xf7])
}
export function decodeLaunchpadFile(input: Uint8Array): LaunchpadMode {
  const bytes = [...input]
  if (bytes.length > 2048 || !header.every((b, i) => bytes[i] === b) || bytes.at(-1) !== 0xf7 || bytes.slice(1, -1).some(b => b > 127)) throw new Error('Unsupported MK3 file header/version/destination or framing')
  const end = bytes.indexOf(0, header.length)
  if (end < 0 || end - header.length > 32 || bytes.slice(header.length, end).some(b => b < 32 || b > 125)) throw new Error('Invalid mode name')
  let at = end + 1
  if (bytes[at++] !== 0 || bytes[at++] !== 127) throw new Error('Widget metadata is outside the single-pad subset')
  if (bytes[at++] !== 0 || bytes[at++] !== 0) throw new Error('Unsupported palette/lightshow settings')
  const onColor = bytes[at++]!
  if (bytes.length - at !== 64 * 8 + 1) throw new Error('Expected 64 complete pad records; faders are not supported by this codec')
  const pads = PAD_ADDRESSES.map(address => {
    const r = bytes.slice(at, at += 8)
    const [wire, channel, type, number, behavior, onValue, offValue, color] = r as [number, number, number, number, number, number, number, number]
    if (wire !== address || !types[type] || ![0, 1, 2].includes(behavior) || (channel > 15 && channel !== 127) || (type === 1 && behavior === 2) || (type === 3 && behavior !== 2)) throw new Error('Unsupported pad record')
    return { address, channel: channel === 127 ? 'global' as const : channel + 1, type: types[type]!, number, behavior: (['momentary', 'toggle', 'trigger'] as const)[behavior]!, onValue, offValue, color }
  })
  return { name: String.fromCharCode(...bytes.slice(header.length, end)), onColor, pads }
}
