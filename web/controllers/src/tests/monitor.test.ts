import { expect, it } from 'vitest'
import { mount } from '@vue/test-utils'
import { readFileSync } from 'node:fs'
import { createElixirMappingEngine } from '../elixir/mapping-context'
import { generateTemplate } from '../elixir/templates'
import { createControllerSimulator, decodeMidi, inspectMidi } from '../midi/monitor'
import MidiMonitor from '../components/MidiMonitor.vue'
const engine = createElixirMappingEngine()
it('simulates all presets with changed channels and resolves every assigned MIDI address', () => {
  for (const preset of ['mixer', 'performance', 'effects', 'sound-editor', 'sequencer'] as const) {
    const template = generateTemplate(preset)
    template.channels = { remoteTracks: 3, remoteGlobal: 7 }
    const mapping = engine.resolve(template), sim = createControllerSimulator(mapping)
    for (const a of mapping.assignments) {
      const button = ['button', 'pad'].includes(a.control.kind)
      const bytes = sim.emit(a.control.id, button ? 'press' : 'value', a.maximum)
      const observed = inspectMidi(bytes, mapping, a.control.id)
      expect(observed.status).toBe('match')
      expect(observed.channel).toBe(a.channel)
      expect(observed.parameters).toContain(a.parameter.name)
      if (button && a.messageType !== 'program-change') expect(decodeMidi(sim.emit(a.control.id, 'release')).value).toBe(a.minimum)
    }
  }
})
it('models note release, toggle state and range enforcement; refuses unverified relative output', () => {
  const t = generateTemplate('mixer'); t.receiver.buttonMode = 'state'
  const a = t.assignments.find(a => a.controlId === 'button.1.1')!
  a.messageType = 'note'; a.buttonMode = 'toggle'
  const mapping = engine.resolve(t), sim = createControllerSimulator(mapping)
  expect(sim.emit(a.controlId, 'press')).toEqual([0x9e, 104, 127])
  expect(sim.emit(a.controlId, 'release')).toEqual([])
  expect(sim.emit(a.controlId, 'press')).toEqual([0x9e, 104, 0])
  const fader = t.assignments.find(a => a.controlId === 'fader.1')!
  fader.range = { minimum: 20, maximum: 90 }
  const ranged = createControllerSimulator(engine.resolve(t))
  expect(() => ranged.emit(fader.controlId, 'value', 19)).toThrow('20–90')
  t.assignments[0]!.encoderMode = 'relative'
  expect(() => createControllerSimulator(engine.resolve(t)).emit(t.assignments[0]!.controlId, 'value', 64)).toThrow('unverified')
  expect(decodeMidi([0x8e, 104, 45])).toMatchObject({ type: 'note', value: 0 })
})
it('distinguishes mismatch, out of range, malformed and system events', () => {
  const m = engine.resolve(generateTemplate('mixer'))
  expect(inspectMidi([0xbf, 8, 64], m, 'fader.1').status).toBe('address mismatch')
  expect(inspectMidi([0xbe, 8, 64], m, 'fader.1').status).toBe('match')
  for (const bytes of [[], [0xbe, 8], [0xbe, 8, 200], [4, 8, 9]]) expect(decodeMidi(bytes).type).toBe('invalid')
  expect(decodeMidi([0xf0, 1, 0xf7]).type).toBe('sysex')
  expect(decodeMidi([0xfa])).toMatchObject({ type: 'realtime', channel: undefined, number: 250 })
})
it('replays all 194 independent-upload physical events through the monitor', () => {
  const capture = JSON.parse(readFileSync('research/hardware/slot1-direct-upload-physical.json', 'utf8'))
  const mapping = engine.resolve(generateTemplate('mixer'))
  expect(capture.messages).toHaveLength(194)
  const groups = new Set<string>()
  for (const m of capture.messages) {
    const result = inspectMidi(m.bytes, mapping)
    expect(result.status).toBe('address match')
    expect(result.parameters.length).toBeGreaterThan(0)
    for (const id of result.controls) groups.add(mapping.assignments.find(a => a.control.id === id)!.control.group)
  }
  expect(groups.size).toBe(6)
})
it('renders simulated and live observations, pauses live input and clears on mapping changes', async () => {
  const t = generateTemplate('mixer')
  const w = mount(MidiMonitor, { props: { template: t } })
  await w.findAll('select')[1]!.setValue('fader.1')
  await w.find('input').setValue(70)
  await w.findAll('button').find(b => b.text() === 'Simulate value')!.trigger('click')
  expect(w.find('tbody').text()).toContain('Simulation')
  await w.findAll('select')[0]!.setValue('fader.1')
  await w.setProps({ incoming: { inputId: 'xl3', timestamp: 42, bytes: [0xbf, 8, 70] }, inputName: 'XL3' })
  expect(w.find('tbody').text()).toContain('address mismatch')
  expect(w.find('tbody').text()).toContain('XL3 (xl3)')
  await w.findAll('button')[0]!.trigger('click')
  await w.setProps({ incoming: { inputId: 'xl3', timestamp: 43, bytes: [0xbe, 8, 70] } })
  expect(w.findAll('tbody tr')).toHaveLength(2)
  await w.setProps({ template: generateTemplate('performance') })
  expect(w.findAll('tbody tr')).toHaveLength(0)
  w.unmount()
})

it('keeps shared-address ambiguity visible and detects values outside edited ranges', () => {
  const t = generateTemplate('mixer')
  const first = t.assignments.find(a => a.controlId === 'fader.1')!
  const second = t.assignments.find(a => a.controlId === 'fader.2')!
  second.parameterId = first.parameterId
  first.range = { minimum: 30, maximum: 80 }
  const m = engine.resolve(t)
  expect(inspectMidi([0xbe, 8, 40], m).status).toBe('ambiguous')
  expect(inspectMidi([0xbe, 8, 20], m, first.controlId).status).toBe('value outside range')
  expect(inspectMidi([0xbe, 8, 40], m).controls).toEqual(['fader.1', 'fader.2'])
})
it('bounds the visible event history to the latest 200 messages', async () => {
  const w = mount(MidiMonitor, { props: { template: generateTemplate('mixer') } })
  for (let i = 0; i < 205; i++) await w.setProps({ incoming: { inputId: 'in', timestamp: i, bytes: [0xbe, 8, 64] } })
  expect(w.findAll('tbody tr')).toHaveLength(200)
  expect(w.find('tbody tr').text()).toContain('204.0')
  await w.findAll('button')[1]!.trigger('click')
  expect(w.findAll('tbody tr')).toHaveLength(0)
  w.unmount()
})
it('validates the live Phase 8 capture and the selected-encoder mismatch results', () => {
  const capture = JSON.parse(readFileSync('research/hardware/phase8-slot1-monitor-physical.json', 'utf8'))
  const mapping = engine.resolve(generateTemplate('mixer'))
  expect(capture.messages).toHaveLength(281)
  expect(capture.selectedOutput).toBe('')
  const values = new Map<number, number[]>()
  for (const event of capture.messages) {
    const observed = inspectMidi(event.bytes, mapping)
    expect(observed.type).toBe('cc')
    expect(observed.channel).toBe(15)
    expect(observed.status).toBe('address match')
    expect(observed.parameters).toHaveLength(1)
    expect(inspectMidi(event.bytes, mapping, 'encoder.1.1').status).toBe(observed.number === 72 ? 'match' : 'address mismatch')
    values.set(observed.number!, [...(values.get(observed.number!) ?? []), observed.value!])
  }
  expect([...values.keys()]).toEqual([72, 8, 104, 112])
  expect(values.get(72)).toHaveLength(67)
  expect(values.get(8)).toHaveLength(210)
  expect(values.get(104)).toEqual([127, 0])
  expect(values.get(112)).toEqual([127, 0])
})
it('keeps the prepared Phase 8 changed-channel mode compatible with the Components re-export', async () => {
  const { decodeFile, encodeFile } = await import('../devices/novation/launch-control-xl3/codec')
  const { XL3_FILE_SERIALIZER } = await import('../devices/novation/launch-control-xl3/adapter')
  const template = JSON.parse(readFileSync('research/hardware/phase8/channel-check.json', 'utf8'))
  const file = new Uint8Array(readFileSync('research/hardware/phase8/channel-check.syx'))
  const exported = new Uint8Array(readFileSync('research/hardware/phase8/phase8-channel-check-components.syx'))
  expect(engine.exportFile(template, XL3_FILE_SERIALIZER)).toEqual(file)
  expect(encodeFile(decodeFile(exported).mode)).toEqual(encodeFile(decodeFile(file).mode))
  const mapping = engine.resolve(template), sim = createControllerSimulator(mapping)
  expect(sim.emit('encoder.1.1', 'value', 64)).toEqual([0xb6, 10, 64])
  expect(sim.emit('fader.1', 'value', 64)).toEqual([0xb2, 8, 64])
  const w = mount(MidiMonitor, { props: { template } })
  expect(w.find('[role=region]').attributes('tabindex')).toBe('0')
  w.unmount()
})
it('physical changed-channel capture covers every group with correct track/global channels', () => {
  const template = JSON.parse(readFileSync('research/hardware/phase8/channel-check.json', 'utf8'))
  const mapping = engine.resolve(template)
  const capture = JSON.parse(readFileSync('research/hardware/phase8-channels-physical.json', 'utf8'))
  expect(capture.messages).toHaveLength(341)
  const expected = new Map([[10, [7,45]], [56,[3,49]], [64,[3,45]], [8,[3,198]], [104,[3,2]], [112,[3,2]]])
  const counts = new Map<number, number>(), groups = new Set<string>()
  for (const event of capture.messages) {
    const observed = inspectMidi(event.bytes, mapping)
    expect(observed.status).toBe('address match')
    expect(observed.channel).toBe(expected.get(observed.number!)![0])
    expect(observed.parameters).toHaveLength(1)
    counts.set(observed.number!, (counts.get(observed.number!) ?? 0) + 1)
    for (const id of observed.controls) groups.add(mapping.assignments.find(a => a.control.id === id)!.control.group)
  }
  expect(groups.size).toBe(6)
  for (const [cc, [, count]] of expected) expect(counts.get(cc)).toBe(count)
  for (const cc of [104,112]) expect(capture.messages.filter((m: { bytes: number[] }) => m.bytes[1] === cc).map((m: {bytes: number[]}) => m.bytes[2])).toEqual([127,0])
})
