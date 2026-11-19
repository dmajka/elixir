/** Conservative hardware subset grounded in the slot1/build1563 captures.
 * Raw hardware read-back is not a canonical export. Unknown metadata/types fail closed.
 */
import { decodeFile, encodeFile, type CustomMode } from './codec'
const prefix = [0xf0, 0, 0x20, 0x29, 2, 0x15, 5, 0]
export type XL3Surface = 0 | 3
export interface XL3Identity { deviceId: number; family: number; member: number; firmwareBuild: number; versionBytes: number[] }
function frame(data: Uint8Array) {
  if (data[0] !== 0xf0 || data[data.length - 1] !== 0xf7 || data.length > 16384 || data.slice(1, -1).some(b => b > 127)) throw new Error('Invalid SysEx framing')
}
export function parseXL3Identity(data: Uint8Array): XL3Identity {
  frame(data)
  if (data.length !== 17 || data[1] !== 0x7e || data[3] !== 6 || data[4] !== 2 || data[5] !== 0 || data[6] !== 0x20 || data[7] !== 0x29 || data[8] !== 0x48 || data[9] !== 1 || data[10] !== 0 || data[11] !== 0) throw new Error('Not the captured XL3 application identity')
  return { deviceId: data[2]!, family: data[8]! | (data[9]! << 7), member: 0, firmwareBuild: data[14]! * 128 + data[15]!, versionBytes: Array.from(data.slice(12, 16)) }
}
/** The address a custom mode's replies carry: slots 1-4 at 6-9 on both surfaces. Slots 5-15 (build 1563, read with
 * index 4-14): surface 0 at 18-28, but surface 3 at another (slot 5: 13); the uploader takes each surface's from the
 * device's reply to its read, and expects the same after. */
export function xl3ReplyAddress(slot: number) {
  if (!Number.isInteger(slot) || slot < 1 || slot > 15) throw new Error('Choose a custom mode from 1 to 15.')
  return slot <= 4 ? slot + 5 : slot + 13
}
function capturedConfiguration(surface: XL3Surface, address: number) {
  if (surface !== 0 && surface !== 3) throw new Error('Unknown surface')
  if (!Number.isInteger(address) || address < 0 || address > 127) throw new Error('Unknown hardware response address')
}
export function isXL3Acknowledgement(data: Uint8Array, surface: XL3Surface, address: number | 'any' = 6): boolean {
  if (address !== 'any') capturedConfiguration(surface, address)
  try { frame(data) } catch { return false }
  return data.length === 12 && prefix.every((b, i) => data[i] === b) && data[8] === 0x15 && data[9] === surface && (address === 'any' || data[10] === address)
}
/** Parse only the captured CC7, explicit-channel subset and known metadata. */
export function decodeXL3ReadBack(frames: readonly Uint8Array[], addresses: number | readonly [number, number] = 6): { mode: CustomMode; raw: Uint8Array } {
  if (frames.length !== 2) throw new Error('Expected two read-back surfaces')
  const canonical: number[] = [], seen = new Set<number>()
  for (const data of frames) {
    frame(data)
    const surface = data[9] as XL3Surface
    const address = typeof addresses === 'number' ? addresses : addresses[surface === 0 ? 0 : 1]
    capturedConfiguration(surface, address)
    if (data.length < 14 || !prefix.every((b, i) => data[i] === b) || data[8] !== 0x10 || data[10] !== address || seen.has(surface)) throw new Error('Unmatched or duplicate read-back surface')
    seen.add(surface)
    canonical.push(...prefix, 0x45, surface, 127)
    let p = 11, metadataSeen = false, nameSeen = false
    const globals = new Map<number, number>()
    while (p < data.length - 1) {
      const tag = data[p++]!, n = data[p++]
      if (n === undefined || p > data.length - 1) throw new Error('Truncated hardware container')
      if (tag === 0x20) {
        if (nameSeen || n > 16 || p + n > data.length - 1) throw new Error('Invalid hardware mode name')
        nameSeen = true; canonical.push(tag, n, ...data.slice(p, p + n)); p += n
      } else if (tag === 0x21) {
        if (metadataSeen || n !== 0) throw new Error('Unsupported hardware metadata')
        metadataSeen = true
      } else if (tag < 0x20) {
        // (1.4.4: a mode's global settings, whatever they hold: not part of the canonical file, which keeps its own;
        // slot 1 had only 6 and 4 on surface 0, 0 1 2 6 4 on surface 3, but other modes carry others)
        if (globals.has(tag)) throw new Error('Duplicate hardware global setting')
        globals.set(tag, n)
      } else if ((tag & 0x60) === 0x40) {
        const length = tag & 31, payload = Array.from(data.slice(p, p + length)); p += length
        if (p > data.length - 1) throw new Error('Truncated hardware control')
        if (!length) { canonical.push(tag, n); continue }
        // Build1563 trims the zero lower endpoint from CC records. No NRPN/note inference.
        if (payload[0] !== 2 || (length !== 8 && length !== 9) || (payload[4]! & 64)) throw new Error('Unsupported hardware control type/channel')
        if (length === 8) payload.push(0)
        canonical.push(0x49, n, ...payload)
      } else if ((tag & 0x60) === 0x60) {
        const length = tag & 31
        if (p + length > data.length - 1) throw new Error('Truncated hardware control name')
        canonical.push(tag, n, ...data.slice(p, p + length)); p += length
      } else throw new Error('Unknown hardware container')
    }
    if (!nameSeen || !metadataSeen) throw new Error('Incomplete hardware metadata')
    canonical.push(0xf7)
  }
  const decoded = decodeFile(Uint8Array.from(canonical))
  return { mode: decoded.mode, raw: Uint8Array.from(frames.flatMap(f => Array.from(f))) }
}
/** All fields must match, allowing only the observed continuous-flag normalization. */
export function compareXL3StoredMode(expected: CustomMode, actual: CustomMode): boolean {
  const normalized = structuredClone(expected)
  normalized.controls.forEach(c => { if (c.id < 48 && c.type === 'cc') c.continuous = true })
  return equalBytes(encodeFile(normalized), encodeFile(actual))
}
export function equalBytes(a: Uint8Array, b: Uint8Array) { return a.length === b.length && a.every((v, i) => v === b[i]) }
