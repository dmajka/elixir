import type { MidiPort } from './provider'
import { createMidiRequestTransport, type MidiRequestTransport, type MidiTransactionTrace } from './request-transport'

export interface CapturedMidiMessage { inputId: string; timestamp: number; bytes: number[] }
/** User-triggered Web MIDI access and capture. Request transports are scoped to an explicit input/output pair. */
export function createWebMidiSession(request = () => navigator.requestMIDIAccess({ sysex: true }), timeoutMs = 10000) {
  let access: MIDIAccess | undefined, disposed = false, connecting = false, generation = 0
  let selectedInput: MIDIInput | undefined
  const transports = new Set<MidiRequestTransport>()
  function disposeTransports() { transports.forEach(t => t.dispose()); transports.clear() }
  const stateListeners = new Set<() => void>(), messageListeners = new Set<(message: CapturedMidiMessage) => void>()
  function notify() { stateListeners.forEach(listener => listener()) }
  function listPorts(): MidiPort[] {
    if (!access) return []
    return [...access.inputs.values(), ...access.outputs.values()].map(p => ({ id: p.id, name: p.name || 'Unnamed MIDI port', manufacturer: p.manufacturer || 'Unknown', type: p.type, state: p.state }))
  }
  const receive = (event: MIDIMessageEvent) => {
    if (!selectedInput || selectedInput.state !== 'connected' || !event.data) return
    const message = { inputId: selectedInput.id, timestamp: event.timeStamp, bytes: Array.from(event.data) }
    messageListeners.forEach(listener => listener({ ...message, bytes: [...message.bytes] }))
  }
  function detachInput() {
    const previous = selectedInput
    selectedInput = undefined
    if (previous) { previous.removeEventListener('midimessage', receive); void previous.close().catch(() => {}) }
  }
  function stateChanged() {
    if (selectedInput && (selectedInput.state === 'disconnected' || access?.inputs.get(selectedInput.id) !== selectedInput)) detachInput()
    notify()
  }
  return {
    get connected() { return !!access },
    get inputId() { return selectedInput?.id },
    listPorts,
    createRequestTransport(inputId: string, outputId: string, onTrace?: (entry: MidiTransactionTrace) => void) {
      const input = access?.inputs.get(inputId), output = access?.outputs.get(outputId)
      if (!access || !input || !output || input.state !== 'connected' || output.state !== 'connected') throw new Error('Select connected MIDI input and output ports.')
      // A new pair invalidates every previous verification/confirmation context.
      disposeTransports()
      const transport = createMidiRequestTransport(access, input, output, 1500, onTrace)
      transports.add(transport)
      return transport
    },
    onStateChange(listener: () => void) { stateListeners.add(listener); return () => { stateListeners.delete(listener) } },
    onMessage(listener: (message: CapturedMidiMessage) => void) { messageListeners.add(listener); return () => { messageListeners.delete(listener) } },
    async connect() {
      if (disposed) throw new Error('MIDI session is closed.')
      if (access) return
      if (connecting) throw new Error('MIDI permission request is already pending.')
      connecting = true
      const attempt = ++generation
      let timer: ReturnType<typeof setTimeout> | undefined
      try {
        const result = await Promise.race([
          request(),
          new Promise<never>((_, reject) => { timer = setTimeout(() => reject(new Error('MIDI permission request timed out. Check the browser permission prompt and try again.')), timeoutMs) }),
        ])
        if (disposed || generation !== attempt) throw new Error('MIDI connection was cancelled.')
        if (!result.sysexEnabled) throw new Error('SysEx permission was not granted. File export is still available.')
        access = result
        access.addEventListener('statechange', stateChanged)
        notify()
      } finally { clearTimeout(timer); connecting = false }
    },
    selectInput(id: string) {
      if (selectedInput?.id === id && selectedInput.state === 'connected') return
      detachInput()
      if (!id) { notify(); return }
      const input = access?.inputs.get(id)
      if (!input || input.state !== 'connected') { notify(); throw new Error('Selected MIDI input is disconnected or unavailable.') }
      selectedInput = input
      input.addEventListener('midimessage', receive)
      // Web MIDI implicitly opens input when a midimessage listener is attached.
      notify()
    },
    disconnect() {
      ++generation
      disposeTransports()
      detachInput()
      access?.removeEventListener('statechange', stateChanged)
      access = undefined
      notify()
    },
    dispose() {
      disposed = true; ++generation
      disposeTransports()
      detachInput(); access?.removeEventListener('statechange', stateChanged); access = undefined
      stateListeners.clear(); messageListeners.clear()
    },
  }
}
