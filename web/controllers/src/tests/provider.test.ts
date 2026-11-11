import { describe, expect, it } from 'vitest'
import { createMockMidiProvider, detectMidiSupport } from '../midi/provider'
describe('MIDI support detection', () => {
  it('rejects insecure contexts even with MIDI support', () => {
    expect(detectMidiSupport({ isSecureContext: false, navigator: { requestMIDIAccess() {} } }).supported).toBe(false)
  })
  it('explains missing browser support', () => {
    expect(detectMidiSupport({ isSecureContext: true, navigator: {} }).reason).toContain('Chrome or Edge')
  })
  it('detects support without requesting permission', () => {
    let called = false
    expect(detectMidiSupport({ isSecureContext: true, navigator: { requestMIDIAccess() { called = true } } }).supported).toBe(true)
    expect(called).toBe(false)
  })
})
describe('mock provider', () => {
  it('provides isolated input/output snapshots without hardware', async () => {
    const provider = createMockMidiProvider()
    const first = await provider.listPorts()
    expect(first.map(p => p.type)).toEqual(['input', 'output'])
    first[0]!.state = 'disconnected'
    expect((await provider.listPorts())[0]!.state).toBe('connected')
  })
})
