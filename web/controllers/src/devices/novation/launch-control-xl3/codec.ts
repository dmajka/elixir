/** Independent XL3 custom-mode codec. File compatibility only; no MIDI I/O.
 * Format evidence and provenance: research/sysex-format.md.
 */
export type Channel = number | 'global'
export type Control = {
  id: number
  name: string
  type: 'off' | 'cc' | 'note' | 'program'
  channel: Channel
  behavior: 'momentary' | 'toggle' | 'trigger' | 'increment' | 'decrement'
  number: number
  min: number
  max: number
  offColor: number
  onColor: number
  sensitivity: number
  relative: boolean
  fixedVelocity: boolean
  velocity: number
  disableTransposition: boolean
  disableLedFeedback: boolean
  continuous: boolean
  bipolar: boolean
}
export interface CustomMode { name: string; controls: Control[] }
export interface DecodedMode { mode: CustomMode; destination: number }
const HEADER = [0xf0, 0, 0x20, 0x29, 2, 0x15, 5, 0, 0x45]
const ids = Array.from({ length: 48 }, (_, i) => i + 16)
function integer(value: number, min: number, max: number, field: string) {
  if (!Number.isInteger(value) || value < min || value > max) throw new Error(`${field} must be ${min}–${max}`)
}
function text(value: string, limit: number): number[] {
  if (typeof value !== 'string' || value.length > limit || !/^[\x20-\x7d]*$/.test(value)) {
    throw new Error(`Name must contain at most ${limit} printable ASCII characters (32–125)`)
  }
  return Array.from(value, c => c.charCodeAt(0))
}
export function createDefaultMode(name = '*New Custom Mode'): CustomMode {
  return { name, controls: ids.map(id => ({
    id, name: '', type: 'cc', channel: 'global', behavior: 'momentary',
    number: id < 40 ? id - 3 : id < 48 ? id - 35 : id - 11,
    min: 0, max: 127, offColor: id < 24 ? 5 : id < 32 ? 9 : id < 40 ? 13 : id < 48 ? 0 : id < 56 ? 25 : 37,
    onColor: id >= 48 ? 3 : 0, sensitivity: 0, relative: false,
    fixedVelocity: id >= 48, velocity: id >= 48 ? 127 : 100,
    disableTransposition: false, disableLedFeedback: false, continuous: false, bipolar: false,
  })) }
}
function record(c: Control): number[] {
  text(c.name, 31)
  if (!['off', 'cc', 'note', 'program'].includes(c.type)) throw new Error('Unsupported control type')
  if (c.type === 'off') return [0x40, c.id]
  if (c.channel !== 'global') integer(c.channel, 1, 16, 'Channel')
  for (const k of ['number', 'min', 'max', 'offColor', 'onColor', 'sensitivity', 'velocity'] as const) integer(c[k], 0, 127, k)
  for (const k of ['relative', 'fixedVelocity', 'disableTransposition', 'disableLedFeedback', 'continuous', 'bipolar'] as const) {
    if (typeof c[k] !== 'boolean') throw new Error(`${k} must be boolean`)
  }
  if (c.id < 48 && c.type !== 'cc') throw new Error('Only buttons support note/program assignments')
  if (c.relative && (c.type !== 'cc' || c.id >= 40)) throw new Error('Relative CC requires an encoder')
  const channel = c.channel === 'global' ? 1 : c.channel - 1
  let flags = (c.channel === 'global' ? 64 : 0) | (c.id >= 48 ? 16 : 0)
  let behavior: number
  if (c.type === 'program') {
    if (!['trigger', 'increment', 'decrement'].includes(c.behavior)) throw new Error('Invalid program behavior')
    behavior = c.behavior === 'trigger' ? 2 : 7
  } else {
    if (!['momentary', 'toggle'].includes(c.behavior)) throw new Error('Invalid CC/note behavior')
    if (c.id < 48 && c.behavior !== 'momentary') throw new Error('Continuous controls cannot toggle')
    behavior = c.behavior === 'toggle' ? 2 : 0
  }
  if (c.type === 'note') {
    flags |= (c.disableTransposition ? 8 : 0) | (c.fixedVelocity ? 4 : 0)
    return [0x48, c.id, 1, c.offColor, c.onColor, channel | ((behavior + 1) << 4), flags, 0, c.fixedVelocity ? c.velocity : 0, c.number]
  }
  flags |= (c.disableLedFeedback ? 32 : 0) | (c.continuous ? 8 : 0) | (c.relative ? 2 : 0) | (c.bipolar ? 1 : 0)
  const high = c.type === 'program' && c.behavior !== 'trigger' ? (c.behavior === 'increment' ? 127 : 0) : (c.type === 'program' ? c.number : c.max)
  const low = c.type === 'program' ? (c.behavior === 'trigger' ? c.number : c.behavior === 'increment' ? 0 : 127) : c.min
  return [0x49, c.id, c.type === 'cc' ? 2 : 3, c.offColor, c.onColor, channel | (behavior << 4), flags,
    c.type === 'cc' ? c.sensitivity : 0, c.type === 'cc' ? c.number : 0, high, low]
}
/** Always generates an export (destination 127). No hardware-transfer API. */
export function encodeFile(mode: CustomMode): Uint8Array {
  const name = text(mode.name, 16)
  if (mode.controls.length !== 48 || mode.controls.some((c, i) => c.id !== ids[i])) throw new Error('Expected controls 16–63 in order')
  const bytes: number[] = []
  for (const [surface, groups] of [[0, [mode.controls.slice(0, 24)]], [3, [mode.controls.slice(24, 32), mode.controls.slice(32)]]] as const) {
    bytes.push(...HEADER, surface, 127, 32, name.length, ...name)
    for (const group of groups) {
      for (const c of group) bytes.push(...record(c))
      for (const c of group) { const label = text(c.name, 31); bytes.push(0x60 | label.length, c.id, ...label) }
    }
    bytes.push(0xf7)
  }
  return Uint8Array.from(bytes)
}
/** Rejects unsupported types/flags instead of silently discarding them. */
export function decodeFile(data: Uint8Array): DecodedMode {
  if (data.length > 16384) throw new Error('Custom mode is too large')
  const frames: Uint8Array[] = []
  let start = 0
  for (let i = 0; i < data.length; i++) {
    if (i === start) { if (data[i] !== 0xf0) throw new Error('Expected SysEx frame') }
    else if (data[i] === 0xf7) { frames.push(data.slice(start, i + 1)); start = i + 1 }
    else if (data[i]! > 127) throw new Error('Non-seven-bit data')
  }
  if (start !== data.length || frames.length !== 2) throw new Error('Expected two complete SysEx frames')
  const mode = createDefaultMode('')
  let destination: number | undefined
  const surfaces = new Set<number>()
  let modeName: string | undefined
  for (const f of frames) {
    if (HEADER.some((v, i) => f[i] !== v) || f.length < 14) throw new Error('Unsupported XL3 header')
    const surface = f[9]!, slot = f[10]!
    if ((surface !== 0 && surface !== 3) || surfaces.has(surface)) throw new Error('Invalid or duplicate surface')
    if (slot !== 127 && slot > 14) throw new Error('Invalid destination')
    if (destination !== undefined && destination !== slot) throw new Error('Mismatched destinations')
    destination = slot; surfaces.add(surface)
    const seen = new Set<number>(), names = new Set<number>()
    let name: string | undefined
    for (let p = 11; p < f.length - 1;) {
      const tag = f[p++]!
      if (p >= f.length - 1) throw new Error('Truncated container')
      const n = f[p++]!
      if (tag === 32) {
        if (name !== undefined || n > 16 || p + n > f.length - 1) throw new Error('Invalid mode name')
        name = String.fromCharCode(...f.slice(p, p + n)); text(name, 16); p += n; continue
      }
      const kind = tag & 0x60, length = tag & 31, id = n
      if (![0x40, 0x60].includes(kind) || id < (surface === 0 ? 16 : 40) || id > (surface === 0 ? 39 : 63) || p + length > f.length - 1) throw new Error('Invalid control container')
      const c = mode.controls[id - 16]!, payload = f.slice(p, p + length); p += length
      const collection = kind === 0x40 ? seen : names
      if (collection.has(id)) throw new Error('Duplicate control container')
      collection.add(id)
      if (kind === 0x60) { c.name = String.fromCharCode(...payload); text(c.name, 31); continue }
      if (!length) { c.type = 'off'; continue }
      const type = payload[0]
      if (![1, 2, 3].includes(type!) || length !== (type === 1 ? 8 : 9)) throw new Error('Unsupported control specification')
      c.type = type === 1 ? 'note' : type === 2 ? 'cc' : 'program'
      const flags = payload[4]!, behavior = payload[3]! >> 4
      if ((flags & 4) && type !== 1) throw new Error('Unsupported resolution')
      c.offColor = payload[1]!; c.onColor = payload[2]!
      c.channel = flags & 64 ? 'global' : (payload[3]! & 15) + 1
      c.fixedVelocity = type === 1 && !!(flags & 4)
      c.disableTransposition = type === 1 && !!(flags & 8)
      c.relative = type === 2 && !!(flags & 2)
      c.disableLedFeedback = type !== 1 && !!(flags & 32)
      c.continuous = type !== 1 && !!(flags & 8)
      c.bipolar = type !== 1 && !!(flags & 1)
      if (type === 1) {
        if (behavior !== 1 && behavior !== 3) throw new Error('Unsupported note behavior')
        c.behavior = behavior === 3 ? 'toggle' : 'momentary'; c.velocity = payload[6]!; c.number = payload[7]!
      } else {
        c.sensitivity = payload[5]!; c.number = payload[6]!; c.max = payload[7]!; c.min = payload[8]!
        if (type === 3) {
          if (behavior !== 2 && behavior !== 7) throw new Error('Unsupported program behavior')
          c.behavior = behavior === 2 ? 'trigger' : c.max > c.min ? 'increment' : 'decrement'
          c.number = c.max
        } else {
          if (behavior !== 0 && behavior !== 2) throw new Error('Unsupported CC behavior')
          c.behavior = behavior === 2 ? 'toggle' : 'momentary'
        }
      }
      // Reconstruct to ensure unsupported flags/reserved bytes are never lost on import.
      const reconstructed = record(c).slice(2)
      if (reconstructed.some((v, i) => v !== payload[i])) throw new Error('Noncanonical or unsupported control data')
    }
    if (name === undefined || seen.size !== 24 || names.size !== 24) throw new Error('Incomplete surface')
    if (modeName !== undefined && modeName !== name) throw new Error('Mismatched mode names')
    modeName = name
  }
  mode.name = modeName!
  return { mode, destination: destination! }
}
