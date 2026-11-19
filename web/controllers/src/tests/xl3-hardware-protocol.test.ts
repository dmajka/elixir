// @vitest-environment node
import { readFileSync } from 'node:fs'
import { expect, it } from 'vitest'
import { parseXL3Identity, isXL3Acknowledgement, decodeXL3ReadBack, compareXL3StoredMode } from '../devices/novation/launch-control-xl3/hardware-protocol'
import { decodeFile } from '../devices/novation/launch-control-xl3/codec'
const read = (path: string) => new Uint8Array(readFileSync(new URL(`../../research/hardware/${path}`, import.meta.url)))
it('validates the observed application identity and decodes firmware build1563', () => {
  const bytes = read('slot1-reconnect/frame-001.syx')
  expect(parseXL3Identity(bytes)).toMatchObject({ deviceId: 0, family: 200, member: 0, firmwareBuild: 1563, versionBytes: [1, 2, 12, 27] })
  for (const index of [0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 16]) { const corrupt = bytes.slice(); corrupt[index] = 99; expect(() => parseXL3Identity(corrupt)).toThrow() }
})
it('matches full captured acknowledgements and rejects other surfaces, addresses and framing', () => {
  const bytes = read('slot1-upload-and-readback/frame-003.syx')
  expect(isXL3Acknowledgement(bytes, 0)).toBe(true)
  expect(isXL3Acknowledgement(bytes, 3)).toBe(false)
  for (let i = 0; i < bytes.length; i++) { const corrupt = bytes.slice(); corrupt[i] = 99; expect(isXL3Acknowledgement(corrupt, 0)).toBe(false) }
  expect(isXL3Acknowledgement(bytes, 0, 7)).toBe(false)   // (slot 2's address: another slot's reply)
  expect(() => isXL3Acknowledgement(bytes, 0, 200)).toThrow('Unknown')
})
it('decodes actual hardware surfaces into the exact Components re-export model', () => {
  const frames = [read('slot1-reconnect/frame-002.syx'), read('slot1-reconnect/frame-003.syx')]
  const decoded = decodeXL3ReadBack(frames)
  expect(decoded.mode).toEqual(decodeFile(read('slot1-Elixir-Mixer-after-reconnect.syx')).mode)
  expect(compareXL3StoredMode(decodeFile(read('elixir-mixer-test.syx')).mode, decoded.mode)).toBe(true)
  decoded.mode.controls[0]!.number++
  expect(compareXL3StoredMode(decodeFile(read('elixir-mixer-test.syx')).mode, decoded.mode)).toBe(false)
  expect(() => decodeXL3ReadBack([frames[0]!, frames[0]!])).toThrow()
  expect(() => decodeXL3ReadBack([frames[0]!])).toThrow()
  const bad = frames.map(f => f.slice()); bad[0]![10] = 7
  expect(() => decodeXL3ReadBack(bad)).toThrow()
})
