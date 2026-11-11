import { afterEach, expect, it, vi } from 'vitest'
import { createWebMidiSession, type CapturedMidiMessage } from '../midi/web-midi'
import { inspectXL3TransferFile } from '../devices/novation/launch-control-xl3/transfer'
import { createElixirMappingEngine } from '../elixir/mapping-context'
import { generateTemplate } from '../elixir/templates'
import { XL3_FILE_SERIALIZER } from '../devices/novation/launch-control-xl3/adapter'
function fakeAccess() {
  const input = Object.assign(new EventTarget(), { id: 'in', name: 'LCXL3', manufacturer: 'Novation', type: 'input', state: 'connected', close: vi.fn(async () => input) })
  const output = Object.assign(new EventTarget(), { id: 'out', name: 'LCXL3', manufacturer: 'Novation', type: 'output', state: 'connected', send: vi.fn() })
  const access = Object.assign(new EventTarget(), { sysexEnabled: true, inputs: new Map([['in', input]]), outputs: new Map([['out', output]]) })
  const request = vi.fn(async () => access as unknown as MIDIAccess)
  return { access, input, output, request }
}
function message(input: EventTarget) { input.dispatchEvent(Object.assign(new Event('midimessage'), { data: Uint8Array.from([0xbe, 8, 100]) })) }
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals() })
it('requests SysEx only on connect, enumerates snapshots, and never sends', async () => {
  const f = fakeAccess(), requestMIDIAccess = vi.fn(f.request)
  vi.stubGlobal('navigator', { requestMIDIAccess })
  const session = createWebMidiSession()
  expect(requestMIDIAccess).not.toHaveBeenCalled()
  await session.connect(); await session.connect()
  expect(requestMIDIAccess).toHaveBeenCalledExactlyOnceWith({ sysex: true })
  expect(session.listPorts().map(p => p.type)).toEqual(['input', 'output'])
  session.listPorts()[0]!.name = 'changed'
  expect(session.listPorts()[0]!.name).toBe('LCXL3')
  expect(f.output.send).not.toHaveBeenCalled()
  expect('send' in session).toBe(false)
  session.dispose()
})
it('captures selected input, isolates listeners, and releases it on disconnect', async () => {
  const f = fakeAccess(), session = createWebMidiSession(f.request)
  const captured: CapturedMidiMessage[] = []
  session.onMessage(m => { m.bytes[0] = 0 })
  session.onMessage(m => captured.push(m))
  await session.connect(); message(f.input)
  expect(captured).toEqual([])
  session.selectInput('in'); message(f.input)
  expect(captured[0]!.bytes).toEqual([0xbe, 8, 100])
  session.disconnect(); message(f.input)
  expect(captured).toHaveLength(1)
  expect(f.input.close).toHaveBeenCalledOnce()
  expect(session.connected).toBe(false)
  expect(session.listPorts()).toEqual([])
  session.dispose()
})
it('clears selected input when removed from the map and requires reselection', async () => {
  const f = fakeAccess(), session = createWebMidiSession(f.request), states = vi.fn()
  session.onStateChange(states)
  await session.connect(); session.selectInput('in')
  f.access.inputs.clear(); f.access.dispatchEvent(new Event('statechange'))
  expect(session.inputId).toBeUndefined()
  expect(states).toHaveBeenCalledTimes(3)
  expect(() => session.selectInput('in')).toThrow('unavailable')
  session.dispose()
})
it('handles permission denial and missing SysEx permission without enabling a session', async () => {
  const denied = createWebMidiSession(async () => { throw new Error('Permission denied') })
  await expect(denied.connect()).rejects.toThrow('Permission denied')
  expect(denied.connected).toBe(false); denied.dispose()
  const f = fakeAccess(); f.access.sysexEnabled = false
  const session = createWebMidiSession(f.request)
  await expect(session.connect()).rejects.toThrow('SysEx permission')
  expect(session.connected).toBe(false); session.dispose()
})
it('times out permission requests and ignores their late resolution', async () => {
  vi.useFakeTimers()
  let resolve!: (access: MIDIAccess) => void
  const session = createWebMidiSession(() => new Promise(r => { resolve = r }), 100)
  const pending = expect(session.connect()).rejects.toThrow('timed out')
  await vi.advanceTimersByTimeAsync(100); await pending
  resolve(fakeAccess().access as unknown as MIDIAccess); await Promise.resolve()
  expect(session.connected).toBe(false)
  session.dispose()
})
it('cancels a pending permission request when the session is disposed', async () => {
  let resolve!: (access: MIDIAccess) => void
  const session = createWebMidiSession(() => new Promise(r => { resolve = r }))
  const pending = session.connect(); session.dispose()
  resolve(fakeAccess().access as unknown as MIDIAccess)
  await expect(pending).rejects.toThrow('cancelled')
  await expect(session.connect()).rejects.toThrow('closed')
})
it('prepares only validated export files and cannot enable an upload', () => {
  const bytes = createElixirMappingEngine().exportFile(generateTemplate('mixer'), XL3_FILE_SERIALIZER)
  const result = inspectXL3TransferFile(bytes, 15)
  expect(result).toMatchObject({ slot: 15, surfaces: 2, uploadEnabled: false, backupAvailable: false, verification: { browserAccepted: 'not-attempted', deviceAcknowledged: 'not-attempted', stored: 'not-verified', physicalOutput: 'not-verified' } })
  expect(result.blockers).toHaveLength(4)
  expect(() => inspectXL3TransferFile(bytes, 0)).toThrow('1 to 15')
  expect(() => inspectXL3TransferFile(bytes, 16)).toThrow('1 to 15')
  expect(() => inspectXL3TransferFile(Uint8Array.of(0xf0, 0xf7), 1)).toThrow()
})
