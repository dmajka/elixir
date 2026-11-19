import { decodeLaunchpadFile } from './codec'
import { mk3SlotIndex, readLaunchpadSlot1 } from './hardware-protocol'
import type { MidiRequestTransport } from '../../../midi/request-transport'

/** Verified browser subset: slot1, build483, explicit-channel momentary CC and note pads (the notes: restoring the
 * user's Drums Square, note pads on channels 1, 4, 5 and 14, read back byte for byte through this same path). */
export const MK3_BROWSER_WRITE_ENABLED = true
export const MK3_TESTED_BUILD = 483
export interface LaunchpadUploadProgress {
  message: string
  acceptedSends: number
  stored: boolean
  restored: boolean
}
const same = (left: Uint8Array, right: Uint8Array) => left.length === right.length && left.every((byte, i) => byte === right[i])

export function prepareLaunchpadWrite(file: Uint8Array, slot = 1): Uint8Array {
  decodeLaunchpadFile(file)
  const frame = file.slice()
  frame[8] = mk3SlotIndex(slot)
  return frame
}
function validateCandidate(file: Uint8Array) {
  const mode = decodeLaunchpadFile(file)
  if (mode.pads.some(p => p.type !== 'off' && ((p.type !== 'cc' && p.type !== 'note') || p.channel === 'global' || p.behavior === 'trigger'))) {
    throw new Error('Sending from the browser supports explicit-channel CC and note pads only (momentary or toggle)')
  }
  return mode
}
function transferPause(signal?: AbortSignal) {
  return new Promise<void>((resolve, reject) => {
    if (signal?.aborted) { reject(new Error('Operation cancelled after send.')); return }
    const abort = () => { clearTimeout(timer); reject(new Error('Operation cancelled after send.')) }
    const timer = setTimeout(() => { signal?.removeEventListener('abort', abort); resolve() }, 100)
    signal?.addEventListener('abort', abort, { once: true })
  })
}

export function createLaunchpadUploader(transport: MidiRequestTransport, onProgress: (state: LaunchpadUploadProgress) => void = () => {}, writesEnabled = MK3_BROWSER_WRITE_ENABLED) {
  let prepared: { token: object; candidate: Uint8Array; original: Uint8Array; deviceId: number; slot: number } | undefined
  let restoration: { candidate: Uint8Array; original: Uint8Array; deviceId: number; slot: number } | undefined
  let busy = false
  let generation = 0
  let state: LaunchpadUploadProgress = { message: '', acceptedSends: 0, stored: false, restored: false }
  function update(message: string, changes: Partial<LaunchpadUploadProgress> = {}) {
    state = { ...state, ...changes, message }
    onProgress({ ...state })
  }
  async function checkedRead(slot: number, signal?: AbortSignal) {
    const read = await readLaunchpadSlot1(transport, signal, slot)
    if (read.identity.firmwareBuild !== MK3_TESTED_BUILD) throw new Error('Browser writes currently require MK3 firmware build483')
    return read
  }
  async function writeAndCompare(file: Uint8Array, slot: number, signal?: AbortSignal) {
    if (!transport.send) throw new Error('Transport does not support a standalone send')
    transport.send(prepareLaunchpadWrite(file, slot), signal)
    update('Browser accepted the mode send. Checking stored the custom mode…', { acceptedSends: state.acceptedSends + 1 })
    await transferPause(signal)
    const read = await checkedRead(slot, signal)
    if (!same(read.backup, file)) throw new Error('Stored the custom mode differs from the sent mode. Keep the backup for restoration.')
    return read
  }
  return {
    invalidate() { generation++; prepared = undefined; restoration = undefined },
    async prepare(file: Uint8Array, signal?: AbortSignal, slot = 1) {
      mk3SlotIndex(slot)
      if (busy) throw new Error('Another MK3 operation is running')
      const currentGeneration = generation
      const candidate = file.slice()
      const mode = validateCandidate(candidate)
      busy = true
      prepared = undefined
      restoration = undefined
      state = { message: '', acceptedSends: 0, stored: false, restored: false }
      try {
        update('Verifying MK3 and backing up the custom mode…')
        const read = await checkedRead(slot, signal)
        if (generation !== currentGeneration) throw new Error('MK3 preparation was invalidated')
        const token = Object.freeze({})
        prepared = { token, candidate, original: read.backup.slice(), deviceId: read.identity.deviceId, slot }
        update('Backup ready.')
        return { token, slot, backup: read.backup.slice(), backupName: read.mode.name, modeName: mode.name, identity: read.identity }
      } finally { busy = false }
    },
    async upload(file: Uint8Array, token: object, signal?: AbortSignal) {
      if (!writesEnabled) throw new Error('MK3 browser writes await explicit acceptance-test approval')
      if (busy || !prepared || token !== prepared.token || !same(file, prepared.candidate)) throw new Error('Prepare this exact mapping and confirm it before writing')
      const plan = prepared
      const currentGeneration = generation
      prepared = undefined
      busy = true
      try {
        update('Rechecking device and original the custom mode…')
        const before = await checkedRead(plan.slot, signal)
        if (before.identity.deviceId !== plan.deviceId || !same(before.backup, plan.original)) throw new Error('Device or the custom mode changed. Prepare a fresh backup.')
        // Retain the original even when a send succeeds but stored verification fails.
        if (generation !== currentGeneration || signal?.aborted) throw new Error('MK3 write preparation was invalidated')
        restoration = plan
        await writeAndCompare(plan.candidate, plan.slot, signal)
        update('Stored the custom mode matches the test mapping. Physical output is ready to check.', { stored: true })
      } finally { busy = false }
    },
    async restore(signal?: AbortSignal) {
      if (!writesEnabled) throw new Error('MK3 browser writes await explicit acceptance-test approval')
      if (busy || !restoration) throw new Error('No original the custom mode backup is bound to this session')
      const plan = restoration
      const currentGeneration = generation
      busy = true
      try {
        update('Checking MK3 before restoring the original the custom mode…')
        const before = await checkedRead(plan.slot, signal)
        if (before.identity.deviceId !== plan.deviceId) throw new Error('Device identity changed before restoration')
        // Restore only the backup retained from this session, never an arbitrary import.
        if (generation !== currentGeneration || signal?.aborted) throw new Error('MK3 restoration was invalidated')
        await writeAndCompare(plan.original, plan.slot, signal)
        restoration = undefined
        update('Original the custom mode restored and stored comparison passed.', { restored: true })
      } finally { busy = false }
    },
  }
}
