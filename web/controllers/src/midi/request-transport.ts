/** One outstanding MIDI request at a time; listeners are installed before sending. */
export interface MidiTransactionTrace { direction: 'sent' | 'received'; requestId: number; timestamp: number; bytes: number[] }
export interface MidiRequestTransport {
  readonly connected: boolean
  exchange(bytes: Uint8Array, accepts: (reply: Uint8Array) => boolean, signal?: AbortSignal, onSent?: () => void): Promise<Uint8Array>
  send?(bytes: Uint8Array, signal?: AbortSignal): void
  dispose(): void
}
export function createMidiRequestTransport(access: MIDIAccess, input: MIDIInput, output: MIDIOutput, timeoutMs = 1500, onTrace?: (entry: MidiTransactionTrace) => void): MidiRequestTransport {
  let requestId = 0
  let disposed = false, pending: ((error: Error) => void) | undefined
  const connected = () => !disposed && access.sysexEnabled && input.state === 'connected' && output.state === 'connected' && access.inputs.get(input.id) === input && access.outputs.get(output.id) === output
  const state = () => { if (!connected()) { disposed = true; pending?.(new Error('MIDI connection changed. The transfer may be partial; no automatic retry was attempted.')) } }
  access.addEventListener('statechange', state)
  return {
    get connected() { return connected() },
    exchange(bytes, accepts, signal, onSent) {
      if (!connected()) return Promise.reject(new Error('MIDI device is disconnected.'))
      if (pending) return Promise.reject(new Error('Another MIDI request is pending.'))
      if (signal?.aborted) return Promise.reject(new Error('Operation cancelled.'))
      const data = bytes.slice(), id = ++requestId
      const trace = (direction: MidiTransactionTrace['direction'], payload: Uint8Array) => {
        try { onTrace?.({ direction, requestId: id, timestamp: Date.now(), bytes: Array.from(payload) }) } catch { /* Observers must not interrupt a transfer. */ }
      }
      return new Promise((resolve, reject) => {
        let timer: ReturnType<typeof setTimeout> | undefined
        const cleanup = () => { clearTimeout(timer); input.removeEventListener('midimessage', receive); signal?.removeEventListener('abort', abort); pending = undefined }
        const fail = (error: Error) => { cleanup(); reject(error) }
        const receive = (event: MIDIMessageEvent) => {
          if (!event.data) return
          const reply = Uint8Array.from(event.data)
          trace('received', reply)
          try { if (accepts(reply)) { cleanup(); resolve(reply) } } catch (e) { fail(e as Error) }
        }
        const abort = () => fail(new Error('Operation cancelled. A send may already have reached the device; no retry was attempted.'))
        pending = fail
        input.addEventListener('midimessage', receive)
        signal?.addEventListener('abort', abort, { once: true })
        timer = setTimeout(() => fail(new Error('MIDI reply timed out. A send may already have reached the device; no automatic retry was attempted.')), timeoutMs)
        try { output.send(data); trace('sent', data); onSent?.() } catch (e) { fail(e as Error) }
      })
    },
    send(bytes, signal) {
      if (!connected()) throw new Error('MIDI device is disconnected.')
      if (pending) throw new Error('Another MIDI request is pending.')
      if (signal?.aborted) throw new Error('Operation cancelled.')
      const data = bytes.slice(), id = ++requestId
      output.send(data)
      try { onTrace?.({ direction: 'sent', requestId: id, timestamp: Date.now(), bytes: Array.from(data) }) } catch { /* Observers do not interrupt sends. */ }
    },
    dispose() {
      if (!disposed) disposed = true
      access.removeEventListener('statechange', state)
      pending?.(new Error('MIDI request session closed.'))
    },
  }
}
