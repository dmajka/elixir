/** Controller families: the Launch Control XL mk2 beside the XL 3, recognized by port name, a template moved between
 * them, and the mk2's connect dialog, which only listens and asks the identity question (no template write). */
import { readFileSync } from 'node:fs'
import { flushPromises, mount } from '@vue/test-utils'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import MidiMapper from '../components/MidiMapper.vue'
import HardwareConnection from '../components/HardwareConnection.vue'
import { convertTemplate, detectModels, IDENTITY_REQUEST, identifyModel } from '../devices/families'
import { LCXL_MK2_PROFILE } from '../devices/novation/launch-control-xl-mk2/profile'
import { decodeMk2Template } from '../devices/novation/launch-control-xl-mk2/codec'
import { XL3_PROFILE } from '../devices/novation/launch-control-xl3/profile'
import { LAUNCHPAD_PRO_MK3_PROFILE } from '../devices/novation/launchpad-pro-mk3/profile'
import { createElixirMappingEngine } from '../elixir/mapping-context'
import { PARAMETER_REGISTRY } from '../elixir/parameter-registry'
import { generateTemplate } from '../elixir/templates'

const typeOf = (id: string) => PARAMETER_REGISTRY.find(p => p.id === id)?.messageType
const engine = createElixirMappingEngine()

describe('recognizing a model', () => {
  it('by its main MIDI port name, not its DAW or DIN ports', () => {
    expect(detectModels(['LCXL3 1 MIDI In'])).toEqual([XL3_PROFILE.id])
    expect(detectModels(['Launch Control XL 3'])).toEqual([XL3_PROFILE.id])
    expect(detectModels(['Launch Control XL'])).toEqual([LCXL_MK2_PROFILE.id])
    expect(detectModels(['Launch Control XL MIDI 1'])).toEqual([LCXL_MK2_PROFILE.id])
    expect(detectModels(['LCXL3 1 DAW In', 'LPProMK3 DIN', 'Launch Control XL HUI'])).toEqual([])
    expect(detectModels(['LPProMK3 MIDI', 'Launch Control XL', 'FM-1'])).toEqual([LCXL_MK2_PROFILE.id, LAUNCHPAD_PRO_MK3_PROFILE.id])
    expect(detectModels(['Launch Control'])).toEqual([])        // (the smaller Launch Control: another layout)
  })
  it('by its identity reply, for the models whose reply was captured', () => {
    const xl3 = new Uint8Array(readFileSync(`${process.cwd()}/research/hardware/slot1-reconnect/frame-001.syx`))
    expect(identifyModel(xl3)).toBe(XL3_PROFILE.id)
    expect(identifyModel(Uint8Array.of(0xf0, 0x7e, 0, 6, 2, 0, 0x20, 0x29, 0x23, 1, 0, 0, 0, 4, 8, 3, 0xf7))).toBe(LAUNCHPAD_PRO_MK3_PROFILE.id)
    const mk2 = new Uint8Array(readFileSync(`${process.cwd()}/research/hardware/lcxl-mk2/identity.syx`))
    expect(identifyModel(mk2)).toBe(LCXL_MK2_PROFILE.id)
    expect(identifyModel(Uint8Array.of(0xf0, 0x7e, 0, 6, 2, 0, 0x20, 0x29, 0x69, 0, 0, 0, 0, 0, 0, 1, 0xf7))).toBeUndefined()   // (another Novation)
    expect(identifyModel(Uint8Array.of(0xf0, 0x7e, 0, 6, 2, 0, 0x21, 0x29, 0x48, 1, 0, 0, 0, 0, 0, 1, 0xf7))).toBeUndefined()
  })
})

describe('a template moved between the XL 3 and the mk2', () => {
  it('keeps every assignment the mk2 can send, without the colors it has not', () => {
    const t = generateTemplate('mixer')
    t.assignments.find(a => a.controlId === 'encoder.1.1')!.colors = { off: 9 }
    const { template, dropped } = convertTemplate(t, LCXL_MK2_PROFILE, typeOf)
    expect(dropped).toEqual([])
    expect(template.assignments).toHaveLength(t.assignments.length)
    expect(template.assignments.find(a => a.controlId === 'encoder.1.1')!.colors).toBeUndefined()
    expect(engine.resolve(template).profile.id).toBe(LCXL_MK2_PROFILE.id)
    expect(convertTemplate(template, XL3_PROFILE, typeOf).template.assignments).toEqual(template.assignments)
  })
  it('drops program changes (the patterns) and relative encoders, which the mk2 has not', () => {
    const t = generateTemplate('sequencer')
    t.assignments.find(a => a.controlId === 'encoder.1.1')!.encoderMode = 'relative'
    const { template, dropped } = convertTemplate(t, LCXL_MK2_PROFILE, typeOf)
    expect(dropped).toEqual(['button.2.5', 'button.2.6', 'button.2.7', 'button.2.8'])
    expect(template.assignments.find(a => a.controlId === 'encoder.1.1')!.encoderMode).toBeUndefined()
    expect(engine.validate(template).valid).toBe(true)
  })
})

describe('the mapper with an mk2', () => {
  beforeEach(() => vi.stubGlobal('ResizeObserver', class { observe() {} disconnect() {} }))
  afterEach(() => vi.unstubAllGlobals())
  it('shows families, keeps the edits on a model change, and offers the device file and sending', async () => {
    const w = mount(MidiMapper)
    expect(w.findAll('.mx-devices button').map(b => b.text())).toEqual(['Launch Control XL', 'Launchpad'])
    await w.findAll('.mx-param')[0]!.trigger('click'); await w.findAll('.control')[8]!.trigger('click')   // a change of one's own
    const mine = (w.emitted('template-change')!.at(-1)![0] as { assignments: unknown[] }).assignments
    await w.find('.mx-model').setValue(LCXL_MK2_PROFILE.id); await flushPromises()
    const t = w.emitted('template-change')!.at(-1)![0] as { deviceProfileId: string; assignments: unknown[] }
    expect(t.deviceProfileId).toBe(LCXL_MK2_PROFILE.id)
    expect(t.assignments).toEqual(mine)
    expect(w.find('.device-mk2').exists()).toBe(true)
    expect(w.find('.mx-primary').text()).toBe('Send to controller')
    await w.find('[aria-controls=mapper-download-options]').trigger('click')
    expect(w.find('#mapper-download-options button').attributes('disabled')).toBeUndefined()
    expect(w.find('#mapper-download-options button').text()).toContain('for Novation Components')
    await w.find('#mapper-tab-global').trigger('click')
    expect(w.text()).not.toContain('Pattern 1')                      // (program changes: not from an mk2's buttons)
    await w.find('#mapper-tab-setup').trigger('click')
    expect(w.find('.mx-steps').text()).toContain('eight user templates')
    expect(w.find('.mx-sheet tbody').text()).toContain('Knob 1/1')
    expect(w.find('.mx-sheet tbody').text()).toMatch(/D1 Reverb send\s*CC \d+\s*15/)
    // the family remembers its model; the XL 3 is one choice away
    await w.findAll('.mx-devices button')[1]!.trigger('click'); await w.findAll('.mx-devices button')[0]!.trigger('click')
    expect(w.find('.device-mk2').exists()).toBe(true)
    await w.find('.mx-model').setValue(XL3_PROFILE.id)
    expect(w.find('.mx-primary').text()).toBe('Send to controller')
    w.unmount()
  })
})

describe('the connect dialog with an mk2', () => {
  afterEach(() => vi.unstubAllGlobals())
  function midi(names: string[], reply?: Uint8Array) {
    const sent: Uint8Array[] = []
    const ports = names.flatMap((name, i) => {
      const input = Object.assign(new EventTarget(), { id: `in${i}`, name, manufacturer: 'Novation', type: 'input', state: 'connected', close: vi.fn(async () => input) })
      const output = Object.assign(new EventTarget(), { id: `out${i}`, name, manufacturer: 'Novation', type: 'output', state: 'connected',
        send: vi.fn((bytes: Uint8Array) => { sent.push(bytes); if (reply) setTimeout(() => input.dispatchEvent(Object.assign(new Event('midimessage'), { data: reply }))) }) })
      return [input, output]
    })
    const access = Object.assign(new EventTarget(), { sysexEnabled: true, inputs: new Map(ports.filter(p => p.type === 'input').map(p => [p.id, p])), outputs: new Map(ports.filter(p => p.type === 'output').map(p => [p.id, p])) })
    vi.stubGlobal('isSecureContext', true); vi.stubGlobal('navigator', { requestMIDIAccess: vi.fn(async () => access) })
    return { sent }
  }
  const mk2Template = () => convertTemplate(generateTemplate('mixer'), LCXL_MK2_PROFILE, typeOf).template

  const settle = async () => { await new Promise(r => setTimeout(r, 5)); await flushPromises() }
  const mk2Identity = () => new Uint8Array(readFileSync(`${process.cwd()}/research/hardware/lcxl-mk2/identity.syx`))

  it('picks the main port (not HUI), checks the identity, then writes the user template and selects it', async () => {
    const { sent } = midi(['FM-1', 'Launch Control XL HUI', 'Launch Control XL'], mk2Identity())
    const w = mount(HardwareConnection, { props: { template: mk2Template(), compact: true } })
    expect(w.find('[aria-label="Destination custom mode"]').exists()).toBe(false)
    await w.find('button').trigger('click'); await flushPromises()
    expect(w.find('.ports-line').text()).toContain('Using Launch Control XL')
    expect(w.find('.ports-line').text()).not.toContain('HUI')
    expect(w.find('.port-selectors').exists()).toBe(false)
    expect(w.find('details.troubleshoot').attributes('open')).toBeUndefined()      // (the check: folded away)
    expect(w.findComponent({ name: 'ControllerCheck' }).exists()).toBe(true)
    expect(w.text()).not.toContain('Verify device')
    const button = () => w.find('.mk2-send-button')
    expect(button().attributes('disabled')).toBeDefined()                 // (not before the confirmation)
    await w.find('[aria-label="Destination user template"]').setValue('3')
    expect(w.emitted('destination-slot')!.at(-1)).toEqual([3])
    await w.find('.overwrite-confirmation input').setValue(true)
    await button().trigger('click'); await settle()
    expect(sent).toHaveLength(3)
    expect(sent[0]).toEqual(IDENTITY_REQUEST)
    const file = decodeMk2Template(sent[1]!)
    expect(file.slot).toBe(2)
    expect(file.continuous[0]!.channel).toBe(15)
    expect(sent[2]).toEqual(Uint8Array.of(0xf0, 0, 0x20, 0x29, 2, 0x11, 0x77, 2, 0xf7))
    expect(w.text()).toContain('Sent to user template 3, and selected.')
    expect(w.find<HTMLInputElement>('.overwrite-confirmation input').element.checked).toBe(false)   // (once per send)
    w.unmount()
  })

  it('sends nothing but the identity question to a port that answers as another controller', async () => {
    const xl3 = new Uint8Array(readFileSync(`${process.cwd()}/research/hardware/slot1-reconnect/frame-001.syx`))
    const { sent } = midi(['Launch Control XL'], xl3)
    const w = mount(HardwareConnection, { props: { template: mk2Template(), compact: true } })
    await w.find('button').trigger('click'); await flushPromises()
    await w.find('.overwrite-confirmation input').setValue(true)
    await w.find('.mk2-send-button').trigger('click'); await settle()
    expect(sent).toEqual([IDENTITY_REQUEST])
    expect(w.find('[role=alert]').text()).toContain('not a Launch Control XL mk2. Nothing was sent.')
    w.unmount()
  })

  it('follows the model on the ports within its family', async () => {
    midi(['Launch Control XL', 'LPProMK3 MIDI'])
    const w = mount(HardwareConnection, { props: { template: generateTemplate('mixer'), compact: true } })
    await w.find('button').trigger('click'); await flushPromises()
    expect(w.emitted('use-model')).toEqual([[LCXL_MK2_PROFILE.id]])
    w.unmount()
  })
})
