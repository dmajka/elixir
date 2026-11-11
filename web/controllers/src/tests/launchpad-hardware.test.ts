import { flushPromises, mount } from '@vue/test-utils'
import LaunchpadTransfer from '../components/LaunchpadTransfer.vue'
import { readFileSync } from 'node:fs'
import { expect, it, vi } from 'vitest'
import { decodeLaunchpadSlot1, isLaunchpadMainPort, MK3_IDENTITY_REQUEST, MK3_SLOT1_READ_REQUEST, parseLaunchpadIdentity, readLaunchpadSlot1 } from '../devices/novation/launchpad-pro-mk3/hardware-protocol'
import type { MidiRequestTransport } from '../midi/request-transport'
const identity = Uint8Array.of(0xf0, 0x7e, 0, 6, 2, 0, 32, 41, 35, 1, 0, 0, 0, 4, 3, 0, 0xf7)
function slotReply() {
  const bytes = new Uint8Array(readFileSync('research/hardware/launchpad-pro-mk3/slot1-Drums-Square-backup.syx'))
  bytes[8] = 4
  return bytes
}
it('validates MK3 application identity and decimal build digits', () => {
  expect(parseLaunchpadIdentity(identity).firmwareBuild).toBe(430)
  for (const [offset, value] of [[8, 72], [9, 17], [12, 10], [14, 2], [16, 0]] as const) {
    const invalid = identity.slice(); invalid[offset] = value
    expect(() => parseLaunchpadIdentity(invalid)).toThrow()
  }
})
it('correlates slot1 using index4 and normalizes only the file destination byte', () => {
  const reply = slotReply()
  const decoded = decodeLaunchpadSlot1(reply)
  expect(decoded.mode.name).toBe('Drums Square')
  expect(decoded.backup).toEqual(new Uint8Array(readFileSync('research/hardware/launchpad-pro-mk3/slot1-Drums-Square-backup.syx')))
  expect(reply[8]).toBe(4)
  reply[8] = 0
  expect(() => decodeLaunchpadSlot1(reply)).toThrow('index4')
})
it('sends only identity and slot read queries through the scoped request transport', async () => {
  const replies = [identity, slotReply()]
  const exchange = vi.fn(async (_bytes, accepts) => {
    const reply = replies.shift()!
    expect(accepts(reply)).toBe(true)
    return reply
  })
  const transport = { connected: true, exchange, dispose: vi.fn() } as MidiRequestTransport
  const result = await readLaunchpadSlot1(transport)
  expect(exchange.mock.calls.map(call => call[0])).toEqual([MK3_IDENTITY_REQUEST, MK3_SLOT1_READ_REQUEST])
  expect(result.mode.name).toBe('Drums Square')
})
it('stops before reading a slot when a different model answers identity', async () => {
  const wrong = identity.slice(); wrong[8] = 72
  const exchange = vi.fn(async () => wrong)
  await expect(readLaunchpadSlot1({ connected: true, exchange, dispose() {} })).rejects.toThrow('MK3')
  expect(exchange).toHaveBeenCalledOnce()
})
it('excludes DAW, DIN and bootloader ports from MK3 main-port selection', () => {
  expect(isLaunchpadMainPort('Launchpad Pro MK3 LPProMK3 MIDI')).toBe(true)
  for (const port of ['LPProMK3 DIN', 'LPProMK3 DAW', 'LPProMK3 Bootloader', 'LCXL3 MIDI']) expect(isLaunchpadMainPort(port)).toBe(false)
})

it('keeps a read active when port enumeration refreshes the same endpoints', async () => {
  let signal: AbortSignal | undefined
  let releaseIdentity: ((bytes: Uint8Array) => void) | undefined
  const exchange = vi.fn(async (_bytes: Uint8Array, _accepts: (bytes: Uint8Array) => boolean, currentSignal?: AbortSignal) => {
    signal = currentSignal
    if (!releaseIdentity) return new Promise<Uint8Array>(resolve => { releaseIdentity = resolve })
    return slotReply()
  })
  const input = { id: 'in', name: 'LPProMK3 MIDI', manufacturer: 'Novation', state: 'connected' as const, type: 'input' as const }
  const output = { ...input, id: 'out', type: 'output' as const }
  const panel = mount(LaunchpadTransfer, { props: { input, output, createTransport: () => ({ connected: true, exchange, dispose: vi.fn() }) } })
  try {
    await panel.find('button').trigger('click')
    await panel.setProps({ input: { ...input }, output: { ...output } })
    expect(signal?.aborted).toBe(false)
    releaseIdentity!(identity)
    await flushPromises()
    expect(panel.text()).toContain('slot 1 Drums Square')
    await panel.setProps({ output: { ...output, state: 'disconnected' } })
    expect(panel.text()).not.toContain('slot 1 Drums Square')
  } finally { panel.unmount() }
})

it('replays actual firmware483 browser identity/read traffic and matches the Components backup', () => {
  const root = 'research/hardware/launchpad-pro-mk3/'
  const trace = JSON.parse(readFileSync(root + 'launchpad-slot1-read-transaction.json', 'utf8'))
  expect(trace.truncated).toBe(false)
  expect(trace.entries.filter((entry: { direction: string }) => entry.direction === 'sent').map((entry: { bytes: number[] }) => entry.bytes)).toEqual([[240,126,127,6,1,247], [240,0,32,41,2,14,5,4,247]])
  const identity = parseLaunchpadIdentity(Uint8Array.from(trace.entries[1].bytes))
  expect(identity).toMatchObject({ firmwareBuild: 483, versionBytes: [0,4,8,3] })
  const read = decodeLaunchpadSlot1(Uint8Array.from(trace.entries[3].bytes))
  const saved = new Uint8Array(readFileSync(root + 'launchpad-slot1-browser-backup.syx'))
  expect(read.backup).toEqual(saved)
  expect(saved).toEqual(new Uint8Array(readFileSync(root + 'slot1-Drums-Square-backup.syx')))
})
