import { afterEach, expect, it, vi } from 'vitest'
import { createMidiRequestTransport } from '../midi/request-transport'
function fixture() {
 const input = Object.assign(new EventTarget(), { id: 'in', state: 'connected' })
 const output = { id: 'out', state: 'connected', send: vi.fn() }
 const access = Object.assign(new EventTarget(), { sysexEnabled: true, inputs: new Map([['in', input]]), outputs: new Map([['out', output]]) })
 const transport = createMidiRequestTransport(access as unknown as MIDIAccess, input as unknown as MIDIInput, output as unknown as MIDIOutput, 100)
 const reply = (bytes: number[]) => input.dispatchEvent(Object.assign(new Event('midimessage'), { data: Uint8Array.from(bytes) }))
 return { input, output, access, transport, reply }
}
afterEach(() => vi.useRealTimers())
it('registers before send, copies bytes and counts browser acceptance separately', async () => {
 const f = fixture(), sent = vi.fn()
 f.output.send.mockImplementation(() => { f.reply([9]); f.reply([1, 2]) })
 expect(await f.transport.exchange(Uint8Array.of(7), b => b[0] === 1, undefined, sent)).toEqual(Uint8Array.of(1, 2))
 expect(sent).toHaveBeenCalledOnce(); f.transport.dispose()
})
it('does not report browser acceptance when send throws', async () => {
 const f = fixture(), sent = vi.fn()
 f.output.send.mockImplementation(() => { throw new Error('send denied') })
 await expect(f.transport.exchange(Uint8Array.of(7), () => true, undefined, sent)).rejects.toThrow('send denied')
 expect(sent).not.toHaveBeenCalled(); f.transport.dispose()
})
it('rejects concurrency, times out unmatched replies, and does not retry', async () => {
 vi.useFakeTimers(); const f = fixture()
 const pending = expect(f.transport.exchange(Uint8Array.of(7), () => false)).rejects.toThrow('timed out')
 await expect(f.transport.exchange(Uint8Array.of(8), () => true)).rejects.toThrow('pending')
 f.reply([1]); await vi.advanceTimersByTimeAsync(100); await pending
 expect(f.output.send).toHaveBeenCalledOnce(); f.transport.dispose()
})
it('aborts and cleans listeners before allowing the next request', async () => {
 const f = fixture(), controller = new AbortController()
 const pending = f.transport.exchange(Uint8Array.of(7), () => true, controller.signal)
 controller.abort(); await expect(pending).rejects.toThrow('cancelled')
 const next = f.transport.exchange(Uint8Array.of(8), () => true); f.reply([2]); expect(await next).toEqual(Uint8Array.of(2))
 f.transport.dispose()
})
it('invalidates on endpoint replacement and rejects subsequent sends', async () => {
 const f = fixture(); const pending = f.transport.exchange(Uint8Array.of(7), () => true)
 f.access.inputs.clear(); f.access.dispatchEvent(new Event('statechange'))
 await expect(pending).rejects.toThrow('connection changed')
 expect(f.transport.connected).toBe(false)
 await expect(f.transport.exchange(Uint8Array.of(8), () => true)).rejects.toThrow('disconnected')
 expect(f.output.send).toHaveBeenCalledOnce(); f.transport.dispose()
})
it('records accepted sent/received bytes with a shared request ID and isolates trace observers', async () => {
 const input = Object.assign(new EventTarget(), { id: 'in', state: 'connected' })
 const output = { id: 'out', state: 'connected', send: vi.fn() }
 const access = Object.assign(new EventTarget(), { sysexEnabled: true, inputs: new Map([['in', input]]), outputs: new Map([['out', output]]) })
 const entries: { direction: string; requestId: number; bytes: number[] }[] = []
 const transport = createMidiRequestTransport(access as unknown as MIDIAccess, input as unknown as MIDIInput, output as unknown as MIDIOutput, 100, entry => { entries.push({ ...entry, bytes: [...entry.bytes] }); entry.bytes[0] = 99; throw new Error('observer failure') })
 const pending = transport.exchange(Uint8Array.of(1,2), b => b[0] === 3)
 input.dispatchEvent(Object.assign(new Event('midimessage'), { data: Uint8Array.of(3,4) }))
 expect(await pending).toEqual(Uint8Array.of(3,4))
 expect(entries).toMatchObject([{ direction: 'sent', requestId: 1, bytes: [1,2] }, { direction: 'received', requestId: 1, bytes: [3,4] }])
 expect(output.send.mock.calls[0]).toEqual([Uint8Array.of(1,2)])
 transport.dispose()
})
it('supports a single accepted send without waiting for an invented acknowledgement', async () => {
 const f = fixture(), bytes = Uint8Array.of(0xf0, 1, 0xf7)
 f.transport.send!(bytes)
 bytes[1] = 2
 expect(f.output.send.mock.calls[0]).toEqual([Uint8Array.of(0xf0, 1, 0xf7)])
 const pending = f.transport.exchange(Uint8Array.of(7), () => true)
 expect(() => f.transport.send!(bytes)).toThrow('pending')
 f.reply([1]); await pending
 f.transport.dispose()
 expect(() => f.transport.send!(bytes)).toThrow('disconnected')
})
