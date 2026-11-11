// @vitest-environment node
import { readFileSync } from 'node:fs'
import { afterEach, expect, it, vi } from 'vitest'
import * as policy from '../devices/novation/launch-control-xl3/hardware-policy'
import { createXL3Uploader, prepareXL3Slot1Frames, isXL3MainPortName, type UploadProgress } from '../devices/novation/launch-control-xl3/upload'
import type { MidiRequestTransport } from '../midi/request-transport'
import { decodeFile, encodeFile } from '../devices/novation/launch-control-xl3/codec'
const read = (path: string) => new Uint8Array(readFileSync(new URL(`../../research/hardware/${path}`, import.meta.url)))
const file = read('elixir-mixer-test.syx')
function simulated() {
 let writes = 0, connected = true
 const frames: Uint8Array[] = [], states: UploadProgress[] = []
 const transport: MidiRequestTransport = {
  get connected() { return connected },
  async exchange(bytes, accepts, signal, onSent) {
   if (signal?.aborted) throw new Error('cancelled')
   const data = bytes.slice(); frames.push(data); onSent?.()
   let reply: Uint8Array
   if (data[1] === 126) reply = read('slot1-reconnect/frame-001.syx')
   else if (data[8] === 64) reply = read(`slot1-${writes ? 'reconnect' : 'readback'}/frame-${data[9] === 0 ? (writes ? '002' : '001') : (writes ? '003' : '002')}.syx`)
   else { writes++; reply = read(`slot1-upload-and-readback/frame-${data[9] === 0 ? '003' : '004'}.syx`) }
   if (!accepts(reply)) throw new Error('simulated unmatched reply')
   return reply
  },
  dispose() { connected = false },
 }
 return { transport, frames, states, uploader: createXL3Uploader(transport, s => states.push(s)) }
}
afterEach(() => vi.restoreAllMocks())
it('prepares a verified identity and backup with only three read-only requests', async () => {
 const f = simulated(), plan = await f.uploader.prepare(file, 1)
 expect(plan.identity.firmwareBuild).toBe(1563)
 expect(decodeFile(plan.backup).mode.name).toBe('Elixir Track Mix')
 expect(f.frames.map(f => f[8] ?? null)).toEqual([null, 64, 64])
 expect(f.uploader.progress.phase).toBe('ready')
 expect(f.uploader.progress.sentSurfaces).toBe(0)
 expect(plan.rawBackup.length).toBe(966)
})
it('denies writes when the internal policy is closed, even with confirmation', async () => {
 vi.spyOn(policy, 'isXL3UploadEnabled').mockReturnValue(false)
 const f = simulated(), plan = await f.uploader.prepare(file, 1)
 await expect(f.uploader.upload(file, 1, plan.token)).rejects.toThrow('Direct upload supports')
 expect(f.frames).toHaveLength(3)
})
it('simulates sequencing, correlated surface replies and post-upload comparison', async () => {
 vi.spyOn(policy, 'isXL3UploadEnabled').mockReturnValue(true) // Simulation only; no browser or hardware.
 const f = simulated(), plan = await f.uploader.prepare(file, 1)
 await f.uploader.upload(file, 1, plan.token)
 expect(f.frames.map(f => f[8] ?? null)).toEqual([null,64,64,null,64,64,69,69,64,64])
 expect(f.uploader.progress).toMatchObject({ phase: 'complete', sentSurfaces: 2, acknowledgedSurfaces: 2, stored: true })
 expect(f.frames.filter(f => f[8] === 69).every(f => f[10] === 0)).toBe(true)
 await expect(f.uploader.upload(file, 1, plan.token)).rejects.toThrow('Prepare again')
})
it('rejects changed mapping/slot/token before any write', async () => {
 vi.spyOn(policy, 'isXL3UploadEnabled').mockReturnValue(true)
 const f = simulated(), plan = await f.uploader.prepare(file, 1)
 const changed = file.slice(); changed[20] = 65
 await expect(f.uploader.upload(changed, 1, plan.token)).rejects.toThrow('changed')
 expect(f.frames).toHaveLength(3)
 const fresh = await f.uploader.prepare(file, 1)
 await expect(f.uploader.upload(file, 1, {})).rejects.toThrow('changed')
 expect(f.frames.filter(f => f[8] === 69)).toHaveLength(0)
 expect(fresh.token).not.toBe(plan.token)
})
it('reports partial success and never retries a missing second surface reply', async () => {
 vi.spyOn(policy, 'isXL3UploadEnabled').mockReturnValue(true)
 const f = simulated(), original = f.transport.exchange.bind(f.transport)
 f.transport.exchange = async (bytes, match, signal, sent) => {
  if (bytes[8] === 69 && bytes[9] === 3) { sent?.(); throw new Error('reply timed out') }
  return original(bytes, match, signal, sent)
 }
 const plan = await f.uploader.prepare(file, 1)
 await expect(f.uploader.upload(file, 1, plan.token)).rejects.toThrow('timed out')
 expect(f.uploader.progress).toMatchObject({ phase: 'failed', sentSurfaces: 2, acknowledgedSurfaces: 1, stored: false })
 expect(f.frames.filter(f => f[8] === 69)).toHaveLength(1)
})
it('rejects unsupported slots, relative mode and other firmware before writes', async () => {
 const f = simulated()
 await expect(f.uploader.prepare(file, 2)).rejects.toThrow('slot1')
 const mode = decodeFile(file).mode; mode.controls[0]!.relative = true
 expect(() => prepareXL3Slot1Frames(encodeFile(mode))).toThrow('absolute CC')
 f.transport.exchange = async () => { const reply = read('slot1-reconnect/frame-001.syx'); reply[15] = 28; return reply }
 await expect(f.uploader.prepare(file, 1)).rejects.toThrow('1564')
 expect(f.frames).toHaveLength(0)
 expect(isXL3MainPortName('LCXL3 1 MIDI In')).toBe(true)
 expect(isXL3MainPortName('LCXL3 1 DAW In')).toBe(false)
 expect(isXL3MainPortName('Launch Control XL MK2')).toBe(false)
})
