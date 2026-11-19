import { isXL3UploadEnabled, XL3_UPLOAD_LIMIT } from './hardware-policy'
import type { MidiRequestTransport } from '../../../midi/request-transport'
import { decodeFile, encodeFile, type CustomMode } from './codec'
import { compareXL3StoredMode, decodeXL3ReadBack, equalBytes, isXL3Acknowledgement, parseXL3Identity, xl3ReplyAddress } from './hardware-protocol'
export interface UploadProgress {
  phase: 'idle' | 'identifying' | 'backing-up' | 'ready' | 'checking' | 'sending' | 'reading-back' | 'complete' | 'failed'
  sentSurfaces: number
  acknowledgedSurfaces: number
  stored: boolean
  message: string
}
const INITIAL: UploadProgress = { phase: 'idle', sentSurfaces: 0, acknowledgedSurfaces: 0, stored: false, message: '' }
const HEADER = [240, 0, 32, 41, 2, 21, 5, 0]
// Slot1/build1563/CC7 only until broader hardware evidence exists. No DAW activation traffic.
export { XL3_UPLOAD_EXPERIMENT_ENABLED, XL3_UPLOAD_LIMIT } from './hardware-policy'
export function isXL3MainPortName(name: string) {
  return /LCXL3|Launch Control XL (?:MK3|3)/i.test(name) && !/DAW|DIN|Bootloader|MIDIIN|MIDIOUT|MIDI [23]/i.test(name)
}
function request(surface: 0 | 3, slot: number) { return Uint8Array.from([...HEADER, 64, surface, slot - 1, 247]) }
// (a reply's address: slots 1-4 the verified one; 5-15 whatever the device answers with, the same on both surfaces)
function readReply(bytes: Uint8Array, surface: 0 | 3, slot: number, address?: number) {   // (slot 5+: per surface)
  return bytes.length >= 14 && HEADER.every((b, i) => bytes[i] === b) && bytes[8] === 16 && bytes[9] === surface && (address !== undefined ? bytes[10] === address : slot > 4 || bytes[10] === xl3ReplyAddress(slot)) && bytes.at(-1) === 247 }
function snapshotFile(bytes: Uint8Array) {
  const file = bytes.slice(), decoded = decodeFile(file)
  if (decoded.destination !== 127) throw new Error('Expected an export file, not a hardware destination.')
  if (decoded.mode.controls.some(c => !['cc', 'off'].includes(c.type) || (c.type !== 'off' && (c.channel === 'global' || c.relative)))) throw new Error('Live verification currently supports explicit-channel absolute CC/off mappings only.')
  return { file, mode: decoded.mode }
}
/** Offline candidate only. Source-derived write slot0; runtime response address6 has separate evidence. */
export function prepareXL3Frames(bytes: Uint8Array, slot = 1): Uint8Array[] {
  xl3ReplyAddress(slot)
  const { file } = snapshotFile(bytes), frames: Uint8Array[] = []
  let start = 0
  for (let i = 0; i < file.length; i++) if (file[i] === 247) { const part = file.slice(start, i + 1); part[10] = slot - 1; frames.push(part); start = i + 1 }
  return frames
}
export const prepareXL3Slot1Frames = (bytes: Uint8Array) => prepareXL3Frames(bytes, 1)
export function createXL3Uploader(transport: MidiRequestTransport, onProgress: (progress: UploadProgress) => void = () => {}) {
  let status = { ...INITIAL }, busy = false, identity: ReturnType<typeof parseXL3Identity> | undefined
  let prepared: { file: Uint8Array; slot: number; mode: CustomMode; backup: Uint8Array; rawBackup: Uint8Array; token: object } | undefined
  const update = (patch: Partial<UploadProgress>) => { status = { ...status, ...patch }; onProgress({ ...status }) }
  async function identify(signal?: AbortSignal) {
    const bytes = await transport.exchange(Uint8Array.of(240, 126, 127, 6, 1, 247), b => {
      if (b.length !== 17 || b[0] !== 240 || b[1] !== 126 || b[3] !== 6 || b[4] !== 2) return false
      // A non-XL3 identity is an explicit failure, not a silently accepted port-name match.
      parseXL3Identity(b); return true
    }, signal)
    const found = parseXL3Identity(bytes)
    if (found.firmwareBuild !== 1563 || found.versionBytes[0] !== 1 || found.versionBytes[1] !== 2) throw new Error(`Firmware build ${found.firmwareBuild} is not in the captured build1563 validation set.`)
    identity = found
    return found
  }
  const learned = new Map<number, [number, number]>()   // (each slot's reply address, surface 0's and 3's)
  async function frames(slot: number, signal?: AbortSignal) {
    const out: Uint8Array[] = []
    for (const surface of [0, 3] as const) out.push(await transport.exchange(request(surface, slot), b => readReply(b, surface, slot, slot > 4 ? undefined : xl3ReplyAddress(slot)), signal))
    learned.set(slot, [out[0]![10]!, out[1]![10]!])
    return out
  }
  async function read(slot: number, signal?: AbortSignal) {
    return decodeXL3ReadBack(await frames(slot, signal), learned.get(slot))
  }
  // (1.4.4: the mode about to be replaced may hold what this decoder doesn't know: notes, relative encoders, other
  // settings. It needn't be understood to be replaced: kept as the device's raw reply, compared byte for byte)
  async function readAny(slot: number, signal?: AbortSignal): Promise<{ mode?: CustomMode; raw: Uint8Array }> {
    const got = await frames(slot, signal)
    const raw = Uint8Array.from(got.flatMap(f => Array.from(f)))
    try { return { mode: decodeXL3ReadBack(got, learned.get(slot)).mode, raw } } catch { return { raw } }
  }
  async function run<T>(operation: () => Promise<T>): Promise<T> {
    if (busy) throw new Error('A device operation is already running.')
    busy = true
    try { return await operation() } catch (e) { prepared = undefined; identity = undefined; update({ phase: 'failed', message: (e as Error).message }); throw e } finally { busy = false }
  }
  return {
    get progress() { return { ...status } },
    async prepare(file: Uint8Array, slot: number, signal?: AbortSignal) {
      return run(async () => {
        prepared = undefined; identity = undefined
        xl3ReplyAddress(slot)
        const snapshot = snapshotFile(file)
        update({ ...INITIAL, phase: 'identifying', message: 'Checking device identity and firmware…' })
        const found = await identify(signal)
        update({ phase: 'backing-up', message: `Reading custom mode ${slot}…` })
        const backup = await readAny(slot, signal)
        const token = Object.freeze({})
        prepared = { ...snapshot, slot, backup: backup.mode ? encodeFile(backup.mode) : backup.raw, rawBackup: backup.raw, token }
        update({ phase: 'ready', message: 'Identity checked; both surfaces backed up. No write performed.' })
        return { token, identity: found, backup: prepared.backup.slice(), rawBackup: backup.raw.slice(), backupName: backup.mode?.name ?? `mode ${slot}`, slot }
      })
    },
    async upload(file: Uint8Array, slot: number, confirmation: object, signal?: AbortSignal) {
      // This gate is not caller-configurable and cannot be bypassed by confirmation JSON.
      if (!isXL3UploadEnabled()) throw new Error(XL3_UPLOAD_LIMIT)
      return run(async () => {
        const plan = prepared
        if (!plan || confirmation !== plan.token || slot !== plan.slot || !equalBytes(file, plan.file) || !transport.connected) throw new Error('Mapping, connection, slot or confirmation changed. Prepare again.')
        prepared = undefined // one-use confirmation, including partial failure
        update({ phase: 'checking', sentSurfaces: 0, acknowledgedSurfaces: 0, stored: false, message: 'Rechecking identity and original slot before overwrite…' })
        await identify(signal)
        const now = await readAny(slot, signal)
        if (!equalBytes(now.raw, plan.rawBackup)) throw new Error('Slot contents changed since backup. Prepare and confirm again.')
        for (const [i, frame] of prepareXL3Frames(plan.file, slot).entries()) {
          update({ phase: 'sending', message: `Sending surface ${i + 1} of2…` })
          // onProgress follows the actual synchronous output.send acceptance in the transport.
          // ACK is tracked separately; failure can leave this or earlier surfaces changed.
          const pendingReply = transport.exchange(frame, b => isXL3Acknowledgement(b, i === 0 ? 0 : 3, slot > 4 ? 'any' : xl3ReplyAddress(slot))   /* (5+: the ack's address unseen yet; one exchange at a time) */, signal, () => update({ sentSurfaces: i + 1 }))
          await pendingReply
          update({ acknowledgedSurfaces: i + 1 })
        }
        update({ phase: 'reading-back', message: 'Comparing both stored surfaces…' })
        const stored = await read(slot, signal)
        if (!compareXL3StoredMode(plan.mode, stored.mode)) throw new Error('Read-back differs from the reviewed mapping. Keep the backup; do not blindly retry.')
        update({ phase: 'complete', stored: true, message: 'Both surfaces acknowledged and read-back matches. Reconnect persistence and physical output need separate checks.' })
        return { mode: stored.mode, identity }
      })
    },
    invalidate() { prepared = undefined; identity = undefined },
  }
}
