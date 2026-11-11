// @vitest-environment node
import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { createMappingEngine, MappingValidationError } from '../core/mapping-engine/engine'
import { exportTemplate, importTemplate } from '../core/mapping-engine/schema'
import type { ControllerTemplate, MappingParameter } from '../core/mapping-engine/types'
import { XL3_FILE_SERIALIZER } from '../devices/novation/launch-control-xl3/adapter'
import { decodeFile, encodeFile } from '../devices/novation/launch-control-xl3/codec'
import { XL3_PROFILE } from '../devices/novation/launch-control-xl3/profile'
import { createElixirMappingEngine, createEmptyTemplate } from '../elixir/mapping-context'
import { PARAMETER_REGISTRY } from '../elixir/parameter-registry'
const engine = createElixirMappingEngine()
const codes = (t: unknown) => engine.validate(t).issues.map(i => i.code)
/** Exhausts all 48 physical groups using a table independent of the serializer. */
function complete(): ControllerTemplate {
  const t = createEmptyTemplate('test-complete', 'Mixer test')
  const tracks = ['d1', 'd2', 'd3', 'sn', 'cy', 'ch', 'oh', 'selected']
  for (let col = 1; col <= 8; col++) {
    for (const [row, parameter] of [[1, 'reverb-send'], [2, 'delay-send'], [3, 'fx-send']] as const) t.assignments.push({ controlId: `encoder.${row}.${col}`, parameterId: `tracks.${tracks[col - 1]}.${parameter}` })
    t.assignments.push({ controlId: `fader.${col}`, parameterId: `tracks.${tracks[col - 1]}.volume` })
    for (const [row, parameter] of [[1, 'mute'], [2, 'solo']] as const) t.assignments.push({ controlId: `button.${row}.${col}`, parameterId: `tracks.${tracks[col - 1]}.${parameter}` })
  }
  return t
}
function one(): ControllerTemplate {
  const t = createEmptyTemplate('one', 'One control')
  t.assignments.push({ controlId: 'encoder.1.1', parameterId: 'tracks.d1.volume' })
  return t
}
describe('complete semantic mapping and file adapter', () => {
  it('resolves and serializes all 48 controls with exact CCs/channels/groups', () => {
    const t = complete(), result = engine.resolve(t)
    expect(result.unassignedControls).toHaveLength(0)
    expect(result.assignments).toHaveLength(48)
    const decoded = decodeFile(engine.exportFile(t, XL3_FILE_SERIALIZER))
    expect(decoded.destination).toBe(127)
    expect(decoded.mode.name).toBe('Mixer test')
    const controls = decoded.mode.controls
    expect(controls.slice(0,24).map(c => c.number)).toEqual([72,73,74,75,76,77,78,79,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71])
    expect(controls.slice(24).map(c => c.number)).toEqual([8,9,10,11,12,13,14,15,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119])
    expect(controls.every(c => c.channel === 15 && c.type === 'cc')).toBe(true)
    expect(controls.slice(32).every(c => c.behavior === 'momentary')).toBe(true)
  })
  it('disables every unassigned control, including an entirely blank custom mode', () => {
    const t = one(), report = engine.validate(t), d = decodeFile(engine.exportFile(t, XL3_FILE_SERIALIZER))
    expect(report.valid).toBe(true); expect(report.unassignedControlIds).toHaveLength(47)
    expect(d.mode.controls.filter(c => c.type === 'off')).toHaveLength(47)
    expect(codes(t)).toContain('unassigned-controls')
    expect(decodeFile(engine.exportFile(createEmptyTemplate('blank','Blank'), XL3_FILE_SERIALIZER)).mode.controls.every(c => c.type === 'off')).toBe(true)
  })
  it('regenerates mixed namespaces after changing channels and preserves value bounds', () => {
    const t = one(); t.channels = { remoteTracks: 3, remoteGlobal: 12 }
    t.assignments.push({ controlId: 'fader.1', parameterId: 'global.factory-kit' })
    const d = decodeFile(engine.exportFile(t, XL3_FILE_SERIALIZER)).mode
    expect(d.controls[0]).toMatchObject({ channel: 3, number: 8 })
    expect(d.controls[24]).toMatchObject({ channel: 12, number: 127, min: 0, max: 84 })
  })
  it('serializes note toggle, program, relative, colors and labels without hardware', () => {
    const t = createEmptyTemplate('mixed', 'Mixed mapping')
    t.receiver = { encoderMode: 'relative', buttonMode: 'state' }; t.destinationSlot = 15
    t.assignments = [
      { controlId: 'encoder.1.1', parameterId: 'global.morph', encoderMode: 'relative', label: 'Morph', colors: { off: 41 } },
      { controlId: 'button.1.1', parameterId: 'tracks.d1.mute', messageType: 'note', buttonMode: 'toggle', colors: { on: 3 } },
      { controlId: 'button.1.2', parameterId: 'global.pattern.32' },
    ]
    const d = decodeFile(engine.exportFile(t, XL3_FILE_SERIALIZER))
    expect(d.destination).toBe(127) // User slot is never written into file export.
    expect(d.mode.controls[0]).toMatchObject({ number: 10, channel: 16, relative: true, offColor: 41, name: 'Morph' })
    expect(d.mode.controls[32]).toMatchObject({ type: 'note', number: 104, channel: 15, behavior: 'toggle' })
    expect(d.mode.controls[33]).toMatchObject({ type: 'program', number: 31, channel: 16, behavior: 'trigger' })
    expect(codes(t)).toContain('relative-unverified')
  })
  it('preserves Phase 2 verified assignments while resolving all device-global channels explicitly', () => {
    const t = createEmptyTemplate('phase2', 'Elixir codec')
    t.receiver = { encoderMode: 'relative', buttonMode: 'state' }
    // Build assignments to reproduce the actual reference data, using an independent temporary semantic registry.
    const fixture = new Uint8Array(readFileSync(new URL('./fixtures/xl3/independent-mixed.syx', import.meta.url)))
    const d = decodeFile(fixture).mode
    const parameters: MappingParameter[] = []
    d.controls.forEach((c, i) => {
      if (c.type === 'off') return
      const id = `fixture.${i}`, type = c.type === 'program' ? 'program-change' : c.type
      parameters.push({ id, name: c.name, shortName: c.name, channelGroup: c.channel === 16 ? 'remoteGlobal' : 'remoteTracks', messageType: type, alternateMessageTypes: [], number: c.number, minimum: c.min, maximum: c.max, controlType: type === 'program-change' ? 'selection' : i >= 32 ? 'button' : 'continuous', buttonBehavior: 'press-or-state' })
      t.assignments.push({ controlId: XL3_PROFILE.controls[i]!.id, parameterId: id, label: c.name, channelOverride: c.channel === 'global' ? 15 : c.channel,
        ...(i < 32 ? { encoderMode: c.relative ? 'relative' as const : 'absolute' as const } : type !== 'program-change' ? { buttonMode: c.behavior as 'momentary' | 'toggle' } : {}) })
    })
    const fixtureEngine = createMappingEngine({ parameters, profiles: [XL3_PROFILE] })
    for (const c of d.controls) if (c.type !== 'off' && c.channel === 'global') c.channel = 15
    expect(fixtureEngine.exportFile(t, XL3_FILE_SERIALIZER)).toEqual(encodeFile(d))
  })
})
describe('compatibility validation', () => {
  it.each([
    ['button.1.1','global.tempo','continuous-on-button'], ['encoder.1.1','global.play-stop','button-on-continuous'],
    ['encoder.1.1','global.pattern.1','device-message-type'], ['button.1.1','system.start','device-message-type'],
    ['button.1.1','global.slot-kit','release-unsafe-selection'], ['no-control','global.tempo','unknown-control'],
    ['encoder.1.1','no-parameter','unknown-parameter'],
  ])('rejects %s → %s', (controlId,parameterId,code) => {
    const t = one(); t.assignments = [{ controlId, parameterId }]
    expect(codes(t)).toContain(code)
    expect(() => engine.exportFile(t, XL3_FILE_SERIALIZER)).toThrow(MappingValidationError)
  })
  it('reports duplicates, namespace overlap and actual message collisions', () => {
    const t = one(); t.assignments.push({ ...t.assignments[0]! })
    expect(codes(t)).toContain('duplicate-control')
    t.assignments[1] = { controlId: 'fader.1', parameterId: 'global.tempo' }; t.channels.remoteTracks = 16
    expect(codes(t)).toContain('channel-overlap'); expect(codes(t)).toContain('duplicate-message')
  })
  it('validates custom ranges, label constraints and supported colors', () => {
    const t = one(); t.assignments[0]!.range = { minimum: 20, maximum: 100 }
    expect(decodeFile(engine.exportFile(t, XL3_FILE_SERIALIZER)).mode.controls[0]).toMatchObject({ min: 20, max: 100 })
    t.assignments[0]!.range.maximum = 128; expect(codes(t)).toContain('parameter-range')
    delete t.assignments[0]!.range; t.assignments[0]!.label = 'é'; expect(codes(t)).toContain('control-label')
    t.assignments[0]!.label = ''; t.assignments[0]!.colors = { on: 3 }; expect(codes(t)).toContain('led-color')
    t.assignments[0]!.colors = { off: 127 }; expect(codes(t)).toContain('led-color')
    t.assignments[0]!.colors = { off: 3 }; expect(engine.validate(t).valid).toBe(true)
    t.name = 'x'.repeat(17); expect(codes(t)).toContain('mode-name')
  })
  it('enforces held/press/state button semantics and encoder capability', () => {
    const t = one(); t.assignments = [{ controlId: 'button.1.1', parameterId: 'tracks.d1.mute', buttonMode: 'toggle' }]
    expect(codes(t)).toContain('button-receiver-mode')
    t.receiver.buttonMode = 'state'; expect(engine.validate(t).valid).toBe(true)
    t.assignments[0]!.parameterId = 'global.roll.d1'; expect(codes(t)).toContain('button-receiver-mode')
    t.assignments = [{ controlId: 'fader.1', parameterId: 'global.tempo', encoderMode: 'relative' }]
    expect(codes(t)).toContain('encoder-mode')
    t.assignments[0]!.controlId = 'encoder.1.1'; t.assignments[0]!.range = { minimum: 0, maximum: 127 }
    expect(codes(t)).toContain('relative-range')
  })
  it('warns on channel overrides, configured conventional overlap and receiver mismatch', () => {
    const t = one(); t.assignments[0]!.channelOverride = 16
    expect(codes(t)).toContain('channel-override')
    t.channels.remoteTracks = 1; expect(codes(t)).toContain('track-overlap')
    t.channels.remoteGlobal = 10; expect(codes(t)).toContain('global-overlap')
    t.assignments[0]!.encoderMode = 'relative'; expect(codes(t)).toContain('encoder-receiver-mode')
    expect(engine.validate(t).valid).toBe(true)
    expect(t.channels.remoteTracks).toBe(1) // warnings never rewrite user configuration
  })
  it('validates slot range, missing channels, type substitutions and serializer selection', () => {
    const t = one(); t.destinationSlot = 16; expect(codes(t)).toContain('slot')
    delete t.destinationSlot; delete t.channels.remoteTracks
    expect(codes(t)).toContain('missing-channel'); expect(codes(t)).toContain('elixir-channels')
    t.channels.remoteTracks = 15; t.assignments[0]!.messageType = 'note'; expect(codes(t)).toContain('parameter-message-type')
    delete t.assignments[0]!.messageType
    expect(() => engine.exportFile(t,{...XL3_FILE_SERIALIZER,deviceProfileId:'other'})).toThrow('Serializer')
    t.deviceProfileId = 'launchpad-unknown'; expect(codes(t)).toContain('unknown-profile')
  })
})
describe('portable schema and state isolation', () => {
  it('round-trips complete edited templates and preserves invalid semantic IDs for editing', () => {
    const t = complete(); t.assignments[0]!.parameterId = 'future.parameter'
    expect(importTemplate(exportTemplate(t))).toEqual(t)
    expect(engine.validate(importTemplate(exportTemplate(t))).valid).toBe(false)
  })
  it.each([
    { schemaVersion: 2 }, { channels: { remoteTracks: 0 } }, { assignments: [{ controlId:'a',parameterId:'b',extra:1 }] },
    { receiver: { encoderMode:'magic',buttonMode:'press' } }, { extra: true }, { destinationSlot: 0 },
    { assignments: [{ controlId:'a',parameterId:'b',range:{minimum:5,maximum:2} }] },
    { assignments: [{ controlId:'a',parameterId:'b',channelOverride:1.5 }] },
  ])('rejects malformed or unknown data: %j', patch => {
    expect(() => importTemplate(JSON.stringify({...one(),...patch}))).toThrow()
    expect(engine.validate({...one(),...patch}).valid).toBe(false)
  })
  it('rejects oversized, null, invalid JSON and prototype-like fields', () => {
    for (const json of ['null','[]','{', ' '.repeat(1_000_001)]) expect(() => importTemplate(json)).toThrow()
    const t = one()
    const json = JSON.stringify(t).replace('"channels":{','"channels":{"__proto__":1,')
    expect(() => importTemplate(json)).toThrow('channel group')
  })
  it('isolates caller templates, resolved results and injected context', () => {
    const profiles = [structuredClone(XL3_PROFILE)], parameters = structuredClone(PARAMETER_REGISTRY)
    const e = createMappingEngine({ profiles, parameters }), t = one(), before = exportTemplate(t)
    const r = e.resolve(t); r.assignments[0]!.label = 'Changed'; r.template.name = 'Changed'
    expect(exportTemplate(t)).toBe(before); expect(e.resolve(t).template.name).toBe('One control')
    profiles[0] = { ...profiles[0]!, controls: [] }
    expect(e.resolve(t).assignments).toHaveLength(1)
  })
  it('supports a non-Elixir virtual profile and registry without a Novation codec', () => {
    const p = { ...XL3_PROFILE, id: 'test.virtual', controls: [{ ...XL3_PROFILE.controls[32]!, id: 'virtual.start', messageTypes: ['realtime' as const] }] }
    const parameter = { id: 'sync.start', name: 'Start', shortName: 'Start', messageType: 'realtime' as const, alternateMessageTypes: [], number: 250, minimum: 0, maximum: 0, controlType: 'realtime' as const }
    const e = createMappingEngine({ profiles: [p], parameters: [parameter] })
    const t = createEmptyTemplate('virtual', 'Virtual'); t.deviceProfileId = p.id; t.channels = {}; t.assignments = [{ controlId: 'virtual.start', parameterId: 'sync.start' }]
    expect(e.resolve(t).assignments[0]).toMatchObject({ messageType: 'realtime', channel: undefined, number: 250 })
    expect(() => e.exportFile(t, XL3_FILE_SERIALIZER)).toThrow('Serializer')
  })
  it('reports automatic label shortening and rejects invalid host registry bytes', () => {
    const p = { ...PARAMETER_REGISTRY[0]!, shortName: 'x'.repeat(32) }
    const e = createMappingEngine({ profiles: [XL3_PROFILE], parameters: [p] })
    const t = one(); t.channels = { remoteTracks: 15 }
    expect(e.validate(t).issues.map(i => i.code)).toContain('label-adjusted')
    expect(e.resolve(t).assignments[0]!.label).toHaveLength(31)
    expect(() => createMappingEngine({ profiles: [XL3_PROFILE], parameters: [{ ...p, number: 128 }] })).toThrow('registry')
  })
  it('rejects duplicated context IDs rather than silently choosing one', () => {
    expect(() => createMappingEngine({parameters:[PARAMETER_REGISTRY[0]!,PARAMETER_REGISTRY[0]!],profiles:[XL3_PROFILE]})).toThrow('duplicate')
    expect(() => createMappingEngine({parameters:PARAMETER_REGISTRY,profiles:[XL3_PROFILE,XL3_PROFILE]})).toThrow('duplicate')
  })
})
