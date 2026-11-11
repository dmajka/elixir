import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { mount } from '@vue/test-utils'
import { blankLaunchpadMode, decodeLaunchpadFile, encodeLaunchpadFile } from '../devices/novation/launchpad-pro-mk3/codec'
import { LAUNCHPAD_PRO_MK3_PROFILE as profile } from '../devices/novation/launchpad-pro-mk3/profile'
import { LAUNCHPAD_PRO_MK3_FILE_SERIALIZER as serializer } from '../devices/novation/launchpad-pro-mk3/adapter'
import { XL3_FILE_SERIALIZER } from '../devices/novation/launch-control-xl3/adapter'
import { createElixirMappingEngine } from '../elixir/mapping-context'
import { generateLaunchpadTemplate, LAUNCHPAD_TEMPLATE_CATALOG } from '../elixir/launchpad-templates'
import { createControllerSimulator, inspectMidi } from '../midi/monitor'
import MidiMapper from '../components/AdminMidiMapper.vue'
const fixture = (name: string) => new Uint8Array(readFileSync(`src/tests/fixtures/launchpad-pro-mk3/${name}.syx`))
const engine = createElixirMappingEngine()
describe('independent MK3 file codec', () => {
  it('matches genuine Components blank and isolated CC/channel/toggle exports', () => {
    const mode = blankLaunchpadMode()
    expect(encodeLaunchpadFile(mode)).toEqual(fixture('components-blank'))
    Object.assign(mode.pads[0]!, { type: 'cc', channel: 1, onValue: 127, color: 5 })
    expect(encodeLaunchpadFile(mode)).toEqual(fixture('components-cc'))
    Object.assign(mode.pads[0]!, { channel: 15, number: 104 })
    expect(encodeLaunchpadFile(mode)).toEqual(fixture('components-cc104-ch15'))
    mode.pads[0]!.behavior = 'toggle'
    expect(encodeLaunchpadFile(mode)).toEqual(fixture('components-cc104-ch15-toggle'))
    Object.assign(mode.pads[1]!, { type: 'note', channel: 1, number: 60, color: 5 })
    expect(encodeLaunchpadFile(mode)).toEqual(fixture('components-note-added'))
    Object.assign(mode.pads[2]!, { type: 'program-change', channel: 1, behavior: 'trigger', color: 5 })
    expect(encodeLaunchpadFile(mode)).toEqual(fixture('components-program-added'))
  })
  it('rejects other-device files, hardware slots, malformed and unsupported widget files', () => {
    const bytes = fixture('components-cc')
    for (const index of [0, 4, 5, 6, 7, 8, 26, 27, 28, 29, 31, 32, 33]) {
      const bad = bytes.slice(); bad[index] = 126
      expect(() => decodeLaunchpadFile(bad)).toThrow()
    }
    expect(() => decodeLaunchpadFile(bytes.slice(0, -8))).toThrow()
    expect(() => encodeLaunchpadFile({ ...blankLaunchpadMode(), name: '🙂' })).toThrow()
    const mode = blankLaunchpadMode(); mode.pads[0]!.channel = 0
    expect(() => encodeLaunchpadFile(mode)).toThrow()
  })
  it('round trips known Components files without losing any bytes', () => {
    for (const n of ['components-blank', 'components-cc', 'components-cc104-ch15', 'components-cc104-ch15-toggle', 'components-note-added', 'components-program-added', 'independent-mixed', 'components-reexport-mixed']) expect(encodeLaunchpadFile(decodeLaunchpadFile(fixture(n)))).toEqual(fixture(n))
  })
})
it('retains every mixed-mode field through Components import/re-export apart from its filename-derived name', () => {
  const independent = decodeLaunchpadFile(fixture('independent-mixed'))
  const reexport = decodeLaunchpadFile(fixture('components-reexport-mixed'))
  expect(reexport.name).toBe('independent-mixed')
  expect({ ...reexport, name: independent.name }).toEqual(independent)
  const backup = new Uint8Array(readFileSync('research/hardware/launchpad-pro-mk3/slot1-Drums-Square-backup.syx'))
  expect(encodeLaunchpadFile(decodeLaunchpadFile(backup))).toEqual(backup)
})
it('uses all 64 physical coordinates and keeps surrounding buttons out of the profile', () => {
  expect(profile.controls).toHaveLength(64)
  expect(profile.controls[0]).toMatchObject({ id: 'pad.1.1', row: 0, column: 0 })
  expect(profile.controls[63]).toMatchObject({ id: 'pad.8.8', row: 7, column: 7 })
  expect(profile.protocol.hardwareUpload).toBe('verified')
})
it('routes every preset through the shared engine, file adapter and detached simulator with changed channels', () => {
  for (const preset of LAUNCHPAD_TEMPLATE_CATALOG) {
    const t = generateLaunchpadTemplate(preset.id, { remoteTracks: 3, remoteGlobal: 7 })
    const mapping = engine.resolve(t), file = decodeLaunchpadFile(engine.exportFile(t, serializer))
    expect(mapping.valid).toBe(true)
    expect(file.pads.filter(p => p.type !== 'off')).toHaveLength(t.assignments.length)
    const simulator = createControllerSimulator(mapping)
    for (const a of mapping.assignments) {
      const bytes = simulator.emit(a.control.id, 'press')
      expect(inspectMidi(bytes, mapping, a.control.id).status).toBe('match')
      const p = file.pads.find(p => p.address === (8 - a.control.row) * 10 + a.control.column + 1)!
      expect(p.channel).toBe(a.channel); expect(p.number).toBe(a.number)
      expect(p.type).toBe(a.messageType)
      if (a.messageType === 'program-change') expect(simulator.emit(a.control.id, 'release')).toEqual([])
    }
    expect(() => engine.exportFile(t, XL3_FILE_SERIALIZER)).toThrow('Serializer does not match')
  }
})
it('maps 32 documented patterns explicitly to program changes 0–31 on Remote Global', () => {
  const t = generateLaunchpadTemplate('pad-patterns')
  const mapping = engine.resolve(t)
  expect(mapping.assignments.map(a => a.number)).toEqual(Array.from({ length: 32 }, (_, i) => i))
  expect(mapping.assignments.every(a => a.messageType === 'program-change' && a.channel === 16)).toBe(true)
})
it('rejects continuous macros on a pad and keeps snapshots isolated', () => {
  const t = generateLaunchpadTemplate('pad-mute-solo')
  t.assignments[0]!.parameterId = 'global.macro-1'
  expect(engine.validate(t).issues.some(i => i.code === 'continuous-on-button')).toBe(true)
  expect(generateLaunchpadTemplate('pad-mute-solo').assignments[0]!.parameterId).toBe('tracks.d1.mute')
})
it('switches devices, renders pad parameters, edits and never exposes the XL3 uploader for MK3', async () => {
  const w = mount(MidiMapper)
  try {
    await w.find('select[aria-label="Controller"]').setValue(profile.id)
    expect(w.findAll('.control')).toHaveLength(64)
    expect(w.text()).toContain('16/64 assigned')
    expect(w.find('.parameter-browser').text()).toContain('D1 Mute')
    expect(w.text()).not.toContain('Verify device & back up slot 1')
    expect(w.text()).toContain('Direct upload supports slot 1, firmware build 483')
    await w.findAll('.setup select')[1]!.setValue('pad-patterns')
    await w.findAll('.control')[31]!.trigger('click')
    expect(w.find('.editor').text()).toContain('program-change 31 · channel 16')
    await w.find('select[aria-label="Controller"]').setValue('novation.launch-control-xl3')
    expect(w.findAll('.control')).toHaveLength(48)
    expect(w.text()).toContain('Verify device & back up slot 1')
  } finally { w.unmount() }
})

it('compares actual Components-written mode1 read-back to the prepared candidate, normalizing only its filename-derived name', () => {
  const root = 'research/hardware/launchpad-pro-mk3/'
  const candidate = decodeLaunchpadFile(new Uint8Array(readFileSync(root + 'elixir-mute-solo.syx')))
  const stored = decodeLaunchpadFile(new Uint8Array(readFileSync(root + 'slot1-mute-solo-readback.syx')))
  expect(stored.name).toBe('elixir-mute-solo')
  expect({ ...stored, name: candidate.name }).toEqual(candidate)
  expect(stored.pads.filter(p => p.type !== 'off')).toHaveLength(16)
})

it('replays all 32 physical mute/solo events from the latched custom-mode browser test', () => {
  const root = 'research/hardware/launchpad-pro-mk3/'
  const capture = JSON.parse(readFileSync(root + 'slot1-quick-tap-mute-solo-physical.json', 'utf8'))
  const mapping = engine.resolve(generateLaunchpadTemplate('pad-mute-solo'))
  expect(capture.excludeMidiClock).toBe(true)
  expect(capture.messages).toHaveLength(32)
  for (let i = 0; i < 16; i++) {
    const assignment = mapping.assignments[i]!
    const press = capture.messages[i * 2].bytes
    const release = capture.messages[i * 2 + 1].bytes
    expect(press).toEqual([190, 104 + i, 127])
    expect(release).toEqual([190, 104 + i, 0])
    expect(inspectMidi(press, mapping, assignment.control.id).status).toBe('match')
    expect(inspectMidi(release, mapping, assignment.control.id).status).toBe('match')
  }
  expect(new Uint8Array(readFileSync(root + 'slot1-quick-tap-restored-readback.syx'))).toEqual(new Uint8Array(readFileSync(root + 'slot1-Drums-Square-backup.syx')))
})
