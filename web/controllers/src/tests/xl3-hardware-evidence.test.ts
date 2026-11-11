// @vitest-environment node
import { readFileSync } from 'node:fs'
import { expect, it } from 'vitest'
import { decodeFile } from '../devices/novation/launch-control-xl3/codec'
const read = (path: string) => new Uint8Array(readFileSync(new URL(`../../research/hardware/${path}`, import.meta.url)))
it('preserves observed Components slot1 upload replies without generalizing their status/address semantics', () => {
  expect(Array.from(read('slot1-upload-and-readback/frame-003.syx'))).toEqual([240, 0, 32, 41, 2, 21, 5, 0, 21, 0, 6, 247])
  expect(Array.from(read('slot1-upload-and-readback/frame-004.syx'))).toEqual([240, 0, 32, 41, 2, 21, 5, 0, 21, 3, 6, 247])
})
it('read-back export matches all test-mode fields except the observed continuous flag normalization', () => {
  const expected = decodeFile(read('elixir-mixer-test.syx'))
  const actual = decodeFile(read('slot1-Elixir-Mixer-readback.syx'))
  expect(actual.destination).toBe(127)
  expect(actual.mode.name).toBe(expected.mode.name)
  expect(actual.mode.controls).toHaveLength(48)
  for (let i = 0; i < 48; i++) {
    expect(actual.mode.controls[i]).toEqual({ ...expected.mode.controls[i]!, continuous: i < 32 })
  }
})
it('representative physical controls emit the Mixer CCs on channel15 with full ranges and momentary releases', () => {
  const packets = readFileSync(new URL('../../research/hardware/slot1-physical-controls.jsonl', import.meta.url), 'utf8').trim().split('\n').map(line => JSON.parse(line) as { source: string; bytes: number[] })
  const values = new Map<number, number[]>()
  for (const packet of packets) {
    expect(packet.source).toBe('LCXL3 1 MIDI Out')
    expect(packet.bytes).toHaveLength(3)
    expect(packet.bytes[0]).toBe(0xbe)
    const cc = packet.bytes[1]!, value = packet.bytes[2]!
    expect(value).toBeGreaterThanOrEqual(0); expect(value).toBeLessThanOrEqual(127)
    values.set(cc, [...(values.get(cc) ?? []), value])
  }
  expect([...values.keys()]).toEqual([72, 8, 104, 112])
  for (const cc of [72, 8]) {
    expect(Math.min(...values.get(cc)!)).toBe(0)
    expect(Math.max(...values.get(cc)!)).toBe(127)
  }
  expect(values.get(104)).toEqual([127, 0])
  expect(values.get(112)).toEqual([127, 0])
})
it('Components slot1 export persists byte-for-byte after user unplug/reconnect', () => {
  expect(read('slot1-Elixir-Mixer-after-reconnect.syx')).toEqual(read('slot1-Elixir-Mixer-readback.syx'))
})
it('actual browser backup is byte-identical to the Components persistence export', () => {
  expect(read('slot1-browser-backup.syx')).toEqual(read('slot1-Elixir-Mixer-after-reconnect.syx'))
})
it('actual direct browser transfer records both writes, replies and matching read-back', async () => {
  const { prepareXL3Slot1Frames } = await import('../devices/novation/launch-control-xl3/upload')
  const { decodeXL3ReadBack, isXL3Acknowledgement } = await import('../devices/novation/launch-control-xl3/hardware-protocol')
  const log = JSON.parse(readFileSync(new URL('../../research/hardware/slot1-direct-upload-transaction.json', import.meta.url), 'utf8')) as { truncated: boolean; progress: object; entries: { direction: string; requestId: number; bytes: number[] }[] }
  expect(log.truncated).toBe(false)
  expect(log.progress).toMatchObject({ phase: 'complete', sentSurfaces: 2, acknowledgedSurfaces: 2, stored: true })
  const writes = log.entries.filter(e => e.direction === 'sent' && e.bytes[8] === 69)
  expect(writes.map(e => Uint8Array.from(e.bytes))).toEqual(prepareXL3Slot1Frames(read('elixir-mixer-test.syx')))
  expect(writes.map(e => e.requestId)).toEqual([7, 8])
  for (const [i, write] of writes.entries()) {
    const reply = log.entries.find(e => e.direction === 'received' && e.requestId === write.requestId)!
    expect(isXL3Acknowledgement(Uint8Array.from(reply.bytes), i === 0 ? 0 : 3)).toBe(true)
  }
  const surfaces = log.entries.filter(e => e.direction === 'received' && e.requestId > 8).map(e => Uint8Array.from(e.bytes))
  expect(decodeXL3ReadBack(surfaces).mode).toEqual(decodeFile(read('slot1-direct-upload-readback.syx')).mode)
  expect(read('slot1-direct-upload-readback.syx')).toEqual(read('slot1-browser-backup.syx'))
})
it('post-direct-upload physical controls match generated Mixer addresses and momentary button values', () => {
  const capture = JSON.parse(readFileSync(new URL('../../research/hardware/slot1-direct-upload-physical.json', import.meta.url), 'utf8')) as { messages: { bytes: number[] }[] }
  const values = new Map<number, number[]>()
  for (const { bytes } of capture.messages) {
    expect(bytes).toHaveLength(3); expect(bytes[0]).toBe(0xbe)
    values.set(bytes[1]!, [...(values.get(bytes[1]!) ?? []), bytes[2]!])
  }
  expect([...values.keys()]).toEqual([72, 56, 64, 8, 104, 112])
  for (const cc of [72,56,64,8]) expect(new Set(values.get(cc)).size).toBeGreaterThan(1)
  expect(values.get(104)).toEqual([127,0]); expect(values.get(112)).toEqual([127,0])
})
it('Phase8 approved test and Mixer restoration traces verify actual writes, acknowledgements and stored mappings', async () => {
  const { prepareXL3Slot1Frames } = await import('../devices/novation/launch-control-xl3/upload')
  const { decodeXL3ReadBack, isXL3Acknowledgement } = await import('../devices/novation/launch-control-xl3/hardware-protocol')
  for (const [trace, file, stored] of [
    ['phase8-test-upload-transaction.json','phase8/channel-check.syx','phase8-test-readback.syx'],
    ['phase8-restore-transaction.json','elixir-mixer-test.syx','phase8-restored-readback.syx'],
  ]) {
    const log = JSON.parse(readFileSync(new URL(`../../research/hardware/${trace}`, import.meta.url), 'utf8')) as { truncated: boolean; progress: object; entries: { direction: string; requestId: number; bytes: number[] }[] }
    expect(log.truncated).toBe(false)
    expect(log.progress).toMatchObject({ phase: 'complete', sentSurfaces: 2, acknowledgedSurfaces: 2, stored: true })
    const writes = log.entries.filter(e => e.direction === 'sent' && e.bytes[8] === 69)
    expect(writes.map(e => Uint8Array.from(e.bytes))).toEqual(prepareXL3Slot1Frames(read(file!)))
    for (const [i,w] of writes.entries()) expect(isXL3Acknowledgement(Uint8Array.from(log.entries.find(e => e.direction === 'received' && e.requestId === w.requestId)!.bytes), i === 0 ? 0 : 3)).toBe(true)
    const lastWrite = Math.max(...writes.map(e => e.requestId))
    const surfaces = log.entries.filter(e => e.direction === 'received' && e.requestId > lastWrite).map(e => Uint8Array.from(e.bytes))
    expect(decodeXL3ReadBack(surfaces).mode).toEqual(decodeFile(read(stored!)).mode)
  }
})
it('Phase8 restores original Mixer and preserves every unrelated slot across the test/restoration sequence', () => {
  expect(read('phase8-restored-readback.syx')).toEqual(read('phase8-before-slot01.syx'))
  expect(read('phase8-before-browser-slot01.syx')).toEqual(read('phase8-before-slot01.syx'))
  for (let slot = 1; slot <= 15; slot++) {
    const suffix = String(slot).padStart(2,'0')
    expect(read(`phase8-after-slot${suffix}.syx`)).toEqual(read(`phase8-before-slot${suffix}.syx`))
  }
})
