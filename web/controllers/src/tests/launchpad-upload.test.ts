import { readFileSync } from 'node:fs'
import { afterEach, expect, it, vi } from 'vitest'
import { createLaunchpadUploader, prepareLaunchpadWrite } from '../devices/novation/launchpad-pro-mk3/upload'
import type { MidiRequestTransport } from '../midi/request-transport'
const root = 'research/hardware/launchpad-pro-mk3/'
const original = new Uint8Array(readFileSync(root + 'slot1-Drums-Square-backup.syx'))
const candidate = new Uint8Array(readFileSync(root + 'elixir-mute-solo.syx'))
const realTrace = JSON.parse(readFileSync(root + 'launchpad-slot1-read-transaction.json', 'utf8'))
const identity = Uint8Array.from(realTrace.entries.find((entry: { direction: string; bytes: number[] }) => entry.direction === 'received' && entry.bytes[1] === 126).bytes)
function simulator() {
  let stored = original.slice()
  const send = vi.fn((frame: Uint8Array) => { expect(frame[8]).toBe(4); stored = frame.slice(); stored[8] = 127 })
  const exchange = vi.fn(async (request: Uint8Array) => {
    if (request[1] === 126) return identity.slice()
    const reply = stored.slice(); reply[8] = 4
    return reply
  })
  const transport: MidiRequestTransport = { connected: true, exchange, send, dispose() {} }
  return { transport, send, replaceStored(bytes: Uint8Array) { stored = bytes.slice() } }
}
afterEach(() => vi.useRealTimers())
it('prepares the source-derived index4 frame without mutating the file', () => {
  const frame = prepareLaunchpadWrite(candidate)
  expect(frame[8]).toBe(4); expect(candidate[8]).toBe(127)
  frame[8] = 127; expect(frame).toEqual(candidate)
})
it('retains a detached original backup, writes once, verifies storage and restores the exact original', async () => {
  vi.useFakeTimers()
  const f = simulator(), progress: { acceptedSends: number; stored: boolean; restored: boolean }[] = []
  const uploader = createLaunchpadUploader(f.transport, state => progress.push(state), true)
  const plan = await uploader.prepare(candidate)
  expect(plan.backup).toEqual(original)
  plan.backup[0] = 0
  const upload = uploader.upload(candidate, plan.token)
  await vi.advanceTimersByTimeAsync(100)
  await upload
  expect(f.send).toHaveBeenCalledOnce()
  expect(progress.at(-1)).toMatchObject({ acceptedSends: 1, stored: true, restored: false })
  await expect(uploader.upload(candidate, plan.token)).rejects.toThrow('Prepare this exact mapping')
  const restore = uploader.restore()
  await vi.advanceTimersByTimeAsync(100)
  await restore
  expect(f.send).toHaveBeenCalledTimes(2)
  expect(f.send.mock.calls[1]![0]).toEqual(prepareLaunchpadWrite(original))
  expect(progress.at(-1)).toMatchObject({ acceptedSends: 2, restored: true })
})
it('honors a disabled write policy and binds confirmation to exact prepared bytes', async () => {
  const f = simulator(), closed = createLaunchpadUploader(f.transport, undefined, false)
  const plan = await closed.prepare(candidate)
  await expect(closed.upload(candidate, plan.token)).rejects.toThrow('approval')
  expect(f.send).not.toHaveBeenCalled()
  const open = createLaunchpadUploader(f.transport, undefined, true)
  const prepared = await open.prepare(candidate), changed = candidate.slice()
  changed[9] = 65
  await expect(open.upload(changed, prepared.token)).rejects.toThrow('exact mapping')
  expect(f.send).not.toHaveBeenCalled()
})
it('rejects changed stored content and invalidated preparation before any write', async () => {
  const f = simulator(), uploader = createLaunchpadUploader(f.transport, undefined, true)
  const plan = await uploader.prepare(candidate)
  f.replaceStored(candidate)
  await expect(uploader.upload(candidate, plan.token)).rejects.toThrow('slot 1 changed')
  expect(f.send).not.toHaveBeenCalled()
  const next = await uploader.prepare(candidate)
  uploader.invalidate()
  await expect(uploader.upload(candidate, next.token)).rejects.toThrow('Prepare')
})
it('rejects untested firmware and non-CC candidate pads before writing', async () => {
  const f = simulator(), uploader = createLaunchpadUploader(f.transport, undefined, true)
  await expect(uploader.prepare(original)).rejects.toThrow('momentary CC')
  const otherFirmware = identity.slice(); otherFirmware[15] = 4
  const normalExchange = f.transport.exchange
  f.transport.exchange = vi.fn(async (request, accepts, signal) => request[1] === 126 ? otherFirmware : normalExchange(request, accepts, signal))
  await expect(uploader.prepare(candidate)).rejects.toThrow('build483')
  expect(f.send).not.toHaveBeenCalled()
})
it('replays the actual browser write trace and stored comparison with the preserved original backup', () => {
  const trace = JSON.parse(readFileSync(root + 'slot1-direct-upload-transaction.json', 'utf8'))
  expect(trace.truncated).toBe(false)
  expect(trace.progress).toMatchObject({ acceptedSends: 1, stored: true, restored: false })
  const writes = trace.entries.filter((entry: { direction: string; bytes: number[] }) => entry.direction === 'sent' && entry.bytes.length > 32)
  expect(writes).toHaveLength(1)
  expect(Uint8Array.from(writes[0].bytes)).toEqual(prepareLaunchpadWrite(candidate))
  const stored = new Uint8Array(readFileSync(root + 'slot1-direct-upload-readback.syx'))
  expect(stored).toEqual(candidate)
  expect(new Uint8Array(readFileSync(root + 'slot1-approved-browser-backup.syx'))).toEqual(original)
})
it('replays the actual browser restoration and recovers the byte-identical original slot', () => {
  const trace = JSON.parse(readFileSync(root + 'slot1-direct-restore-transaction.json', 'utf8'))
  expect(trace.truncated).toBe(false)
  expect(trace.progress).toMatchObject({ acceptedSends: 2, stored: true, restored: true })
  const writes = trace.entries.filter((entry: { direction: string; bytes: number[] }) => entry.direction === 'sent' && entry.bytes.length > 32)
  expect(writes).toHaveLength(2)
  expect(Uint8Array.from(writes[1].bytes)).toEqual(prepareLaunchpadWrite(original))
  expect(new Uint8Array(readFileSync(root + 'slot1-direct-restored-readback.syx'))).toEqual(original)
})
