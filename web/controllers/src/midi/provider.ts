/** Framework-independent transport boundary. No hardware writes in Phase 0. */
export interface MidiPort {
  id: string
  name: string
  manufacturer: string
  type: 'input' | 'output'
  state: 'connected' | 'disconnected'
}
export interface MidiProvider {
  readonly kind: 'mock' | 'web-midi'
  listPorts(): Promise<MidiPort[]>
}
export interface MidiEnvironment {
  isSecureContext?: boolean
  navigator?: { requestMIDIAccess?: unknown }
}
export function detectMidiSupport(env: MidiEnvironment = globalThis): {
  supported: boolean; reason: string
} {
  if (!env.isSecureContext) return { supported: false, reason: 'Web MIDI needs HTTPS or localhost.' }
  if (typeof env.navigator?.requestMIDIAccess !== 'function') {
    return { supported: false, reason: 'Web MIDI is unavailable. Use desktop Chrome or Edge.' }
  }
  return { supported: true, reason: 'Web MIDI is available. SysEx permission will be requested when connecting hardware.' }
}
export function createMockMidiProvider(): MidiProvider {
  const ports: MidiPort[] = ['input', 'output'].map(type => ({
    id: `mock-xl3-${type}`, name: 'Launch Control XL 3 (simulation)',
    manufacturer: 'Mock Novation', type: type as MidiPort['type'], state: 'connected',
  }))
  return { kind: 'mock', async listPorts() { return ports.map(port => ({ ...port })) } }
}
