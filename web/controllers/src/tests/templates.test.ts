// @vitest-environment node
import { describe, expect, it } from 'vitest'
import { TEMPLATE_CATALOG, generateTemplate, createUserTemplate, editTemplateAssignment } from '../elixir/templates'
import { createElixirMappingEngine } from '../elixir/mapping-context'
import { exportTemplate, importTemplate } from '../core/mapping-engine/schema'
import { XL3_FILE_SERIALIZER } from '../devices/novation/launch-control-xl3/adapter'
import { decodeFile } from '../devices/novation/launch-control-xl3/codec'
const engine = createElixirMappingEngine()
describe('automatic Elixir templates', () => {
  for (const preset of TEMPLATE_CATALOG) it(`${preset.id}: complete, independent, serializable mapping`, () => {
    const template = generateTemplate(preset.id, { remoteTracks: 14, remoteGlobal: 8 })
    const mapping = engine.resolve(template)
    expect(mapping.assignments).toHaveLength(48)
    expect(mapping.unassignedControls).toEqual([])
    expect(mapping.issues).toEqual([])
    const decoded = decodeFile(engine.exportFile(template, XL3_FILE_SERIALIZER)).mode
    expect(decoded.controls).toHaveLength(48)
    for (const a of mapping.assignments) {
      const index = mapping.profile.controls.findIndex(c => c.id === a.control.id)
      const wire = decoded.controls[index]!
      expect(wire.number).toBe(a.number)
      expect(wire.channel).toBe(a.parameter.channelGroup === 'remoteTracks' ? 14 : 8)
      expect(wire.type).toBe(a.messageType === 'program-change' ? 'program' : a.messageType)
      if (a.messageType !== 'program-change') expect([wire.min, wire.max]).toEqual([a.parameter.minimum, a.parameter.maximum])
      expect(wire.relative).toBe(false)
    }
    expect(importTemplate(exportTemplate(template))).toEqual(template)
    template.assignments[0]!.parameterId = 'broken'
    expect(engine.validate(generateTemplate(preset.id)).valid).toBe(true)
  })
  it('Mixer uses the remote chart including separate CH, OH and selected columns', () => {
    const m = engine.resolve(generateTemplate('mixer'))
    const numbers = (group: string) => m.assignments.filter(a => a.control.id.startsWith(group)).map(a => a.number)
    expect(numbers('fader.')).toEqual([8, 9, 10, 11, 12, 13, 14, 15])
    expect(numbers('encoder.1.')).toEqual([72, 73, 74, 75, 76, 77, 78, 79])
    expect(numbers('encoder.2.')).toEqual([56, 57, 58, 59, 60, 61, 62, 63])
    expect(numbers('encoder.3.')).toEqual([64, 65, 66, 67, 68, 69, 70, 71])
    expect(numbers('button.1.')).toEqual([104, 105, 106, 107, 108, 109, 110, 111])
    expect(numbers('button.2.')).toEqual([112, 113, 114, 115, 116, 117, 118, 119])
    expect(m.assignments.every(a => a.channel === 15)).toBe(true)
  })
  it('preserves global pattern program triggers and global defaults', () => {
    const m = engine.resolve(generateTemplate('sequencer'))
    const patterns = m.assignments.filter(a => a.messageType === 'program-change')
    expect(patterns.map(a => a.number)).toEqual([0, 1, 2, 3])
    expect(patterns.every(a => a.channel === 16)).toBe(true)
  })
  it('keeps all six sound parameter families', () => {
    const ids = generateTemplate('sound-editor').assignments.map(a => a.parameterId)
    for (const key of ['filter-frequency', 'resonance', 'pitch', 'decay', 'sample-rate', 'pan']) expect(ids.some(id => id.endsWith(`.${key}`))).toBe(true)
  })
  it('rejects invalid channels and exposes overlaps without rewriting channels', () => {
    expect(() => generateTemplate('effects', { remoteTracks: 0, remoteGlobal: 16 })).toThrow()
    const t = generateTemplate('performance', { remoteTracks: 15, remoteGlobal: 15 })
    expect(t.channels).toEqual({ remoteTracks: 15, remoteGlobal: 15 })
    expect(engine.validate(t).issues.some(i => i.code === 'channel-overlap')).toBe(true)
  })
  it('supports detached user templates, edits, removal and disabled exports', () => {
    const source = generateTemplate('mixer')
    const user = createUserTemplate('mine', 'My mixer', source.assignments)
    const edited = editTemplateAssignment(user, 'fader.1', { parameterId: 'global.master-volume', label: 'Master', range: { minimum: 10, maximum: 100 } })
    expect(source.assignments[24]).toEqual(user.assignments[24])
    expect(user.assignments.find(a => a.controlId === 'fader.1')!.parameterId).toBe('tracks.d1.volume')
    expect(engine.resolve(edited).assignments.find(a => a.control.id === 'fader.1')).toMatchObject({ channel: 16, minimum: 10, maximum: 100, label: 'Master' })
    const partial = editTemplateAssignment(edited, 'fader.1', null)
    expect(engine.validate(partial).unassignedControlIds).toEqual(['fader.1'])
    expect(decodeFile(engine.exportFile(partial, XL3_FILE_SERIALIZER)).mode.controls[24]!.type).toBe('off')
    expect(engine.validate(createUserTemplate('empty', 'Empty')).unassignedControlIds).toHaveLength(48)
    expect(() => editTemplateAssignment(user, 'fader.1', { parameterId: 'global.fill' })).toThrow()
    expect(() => editTemplateAssignment(user, 'unknown', null)).toThrow()
    expect(() => createUserTemplate('bad', 'Bad', [{ controlId: 'button.1.1', parameterId: 'system.clock' }])).toThrow()
  })
})
