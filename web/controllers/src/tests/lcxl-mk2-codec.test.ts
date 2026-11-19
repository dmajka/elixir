/** The mk2's template file against Components' own exports (research/hardware/lcxl-mk2): each recorded change is
 * reproduced byte for byte by setting that one field, and an Elixir template becomes a file Components can import. */
import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { decodeMk2Template, defaultMk2Template, encodeMk2Template } from '../devices/novation/launch-control-xl-mk2/codec'
import { LCXL_MK2_FILE_SERIALIZER } from '../devices/novation/launch-control-xl-mk2/adapter'
import { LCXL_MK2_PROFILE } from '../devices/novation/launch-control-xl-mk2/profile'
import { convertTemplate } from '../devices/families'
import { createElixirMappingEngine } from '../elixir/mapping-context'
import { PARAMETER_REGISTRY } from '../elixir/parameter-registry'
import { generateTemplate } from '../elixir/templates'
const rec = (name: string) => new Uint8Array(readFileSync(`${process.cwd()}/research/hardware/lcxl-mk2/${name}.syx`))
const LAST = 8 + 56 * 6                                   // (Components' own section: compared only for (a), (c), (f))

describe('the Launch Control XL mk2 template file', () => {
  it('writes a new template exactly as Components exports it', () => {
    expect(encodeMk2Template(defaultMk2Template())).toEqual(rec('a-new-template'))
    expect(decodeMk2Template(rec('a-new-template'))).toEqual(defaultMk2Template())
  })
  it('puts a knob\'s channel, a button\'s channel and toggle where Components does', () => {
    const c = defaultMk2Template(); c.continuous[0]!.channel = 9
    expect(encodeMk2Template(c)).toEqual(rec('c-knob1-channel9'))
    const f = defaultMk2Template(); f.buttons[0]!.channel = 9
    expect(encodeMk2Template(f)).toEqual(rec('f-button1-channel9'))
    const e = defaultMk2Template(); e.buttons[0]!.toggle = true
    expect(encodeMk2Template(e).slice(0, LAST)).toEqual(rec('e2-button1-toggle').slice(0, LAST))
    expect(decodeMk2Template(rec('e2-button1-toggle')).buttons[0]!.toggle).toBe(true)
  })
  it('turns the Elixir Mixer into a file: CCs on the TRACKS channel, mutes and solos as CC buttons', () => {
    const typeOf = (id: string) => PARAMETER_REGISTRY.find(p => p.id === id)?.messageType
    const t = convertTemplate(generateTemplate('mixer', { remoteTracks: 15, remoteGlobal: 16 }), LCXL_MK2_PROFILE, typeOf).template
    const file = createElixirMappingEngine().exportFile(t, LCXL_MK2_FILE_SERIALIZER)
    const d = decodeMk2Template(file)
    const reverb = PARAMETER_REGISTRY.find(p => p.id === 'tracks.d1.reverb-send')!, mute = PARAMETER_REGISTRY.find(p => p.id === 'tracks.d1.mute')!
    expect(d.slot).toBe(0)
    expect(d.continuous[0]).toEqual({ number: reverb.number, min: 0, max: 127, channel: 15 })
    expect(d.buttons[0]).toMatchObject({ type: 'cc', number: mute.number, channel: 15, toggle: true })   // (1.4.4: mutes and solos toggle)
    expect(d.buttons.slice(16)).toEqual(defaultMk2Template().buttons.slice(16))   // (the side buttons: as Components has them)
  })
  it('refuses what it cannot write or read', () => {
    const t = defaultMk2Template(); t.continuous[0]!.channel = 17
    expect(() => encodeMk2Template(t)).toThrow('Channel')
    expect(() => decodeMk2Template(rec('a-new-template').slice(0, 100))).toThrow('Not a Launch Control XL mk2')
  })
})
