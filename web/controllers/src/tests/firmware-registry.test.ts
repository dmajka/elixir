/** The registry comes from the firmware (tools/remote_params.py). Here a copy of the firmware with one CC moved
 * (RING's Tone, RM_XK: CC 5 -> 6) is generated, and the moved number is followed into the registry, the presets,
 * the parameter browser, the resolved mapping, the .syx file and the simulator. No hardware is touched. */
import { execFileSync, spawnSync } from 'node:child_process'
import { copyFileSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { flushPromises, mount } from '@vue/test-utils'
import { afterEach, beforeAll, describe, expect, it, vi } from 'vitest'
import { parseParameterDocument, type ParameterDocument } from '../elixir/registry-schema'

const ELIXIR = join(process.cwd(), '../..')   // (vitest runs in web/controllers)
const PY = process.env.PYTHON || 'python3'
const FILES = ['remote.c', 'midi.c', 'seq.c', 'kit.c', 'elixir.c', 'engine.c', 'dsp.c']

function firmwareCopy(edit: (remote: string) => string) {
  const dir = mkdtempSync(join(tmpdir(), 'elixir-src-'))
  for (const f of FILES) copyFileSync(join(ELIXIR, 'firmware/src', f), join(dir, f))
  writeFileSync(join(dir, 'remote.c'), edit(readFileSync(join(dir, 'remote.c'), 'utf8')))
  return dir
}
function generate(src?: string) {
  const out = mkdtempSync(join(tmpdir(), 'elixir-params-'))
  const r = spawnSync(PY, [join(ELIXIR, 'tools/remote_params.py'), ...(src ? ['--src', src] : []), out], { encoding: 'utf8' })
  return { ...r, out, json: () => readFileSync(join(out, 'elixir-midi-parameters.json')) }
}
const moveRingTone = (s: string) => {
  const moved = s.replace('RM_XK[RM_NXK] = {3, 2, 4, 5, 7,', 'RM_XK[RM_NXK] = {3, 2, 4, 6, 7,')
  if (moved === s) throw new Error('remote.c: RM_XK not as this test expects')
  return moved
}

/** the mapper's modules, loaded against another registry document */
async function withRegistry(document: ParameterDocument) {
  vi.resetModules()
  vi.doMock('virtual:elixir-parameters', () => ({ default: document }))
  return {
    registry: await import('../elixir/parameter-registry'),
    templates: await import('../elixir/templates'),
    context: await import('../elixir/mapping-context'),
    adapter: await import('../devices/novation/launch-control-xl3/adapter'),
    codec: await import('../devices/novation/launch-control-xl3/codec'),
    monitor: await import('../midi/monitor'),
    admin: (await import('../components/AdminMidiMapper.vue')).default,
  }
}
afterEach(() => { vi.doUnmock('virtual:elixir-parameters'); vi.resetModules() })

describe('the registry generated from the firmware', () => {
  let current: Buffer
  beforeAll(() => {
    const r = generate()
    expect(r.status, r.stderr).toBe(0)
    current = r.json()
  })

  it('is byte-identical when generated twice, and the manifest hashes the bytes', () => {
    const again = generate()
    expect(again.json().equals(current)).toBe(true)
    const meta = JSON.parse(readFileSync(join(again.out, 'elixir-midi-parameters.meta.json'), 'utf8'))
    const sha = execFileSync('shasum', ['-a', '256', join(again.out, 'elixir-midi-parameters.json')], { encoding: 'utf8' }).split(' ')[0]
    expect(meta).toMatchObject({ schemaVersion: 1, sha256: sha, parameterCount: 311 })
    const doc = parseParameterDocument(JSON.parse(current.toString('utf8')))
    expect(doc.parameters.map(p => p.id)).toEqual([...doc.parameters.map(p => p.id)].sort())   // (sorted by id)
  })

  it('follows a CC moved in the firmware into the browser, presets, mapping, .syx and simulator', async () => {
    const r = generate(firmwareCopy(moveRingTone))
    expect(r.status, r.stderr).toBe(0)
    const moved = parseParameterDocument(JSON.parse(r.json().toString('utf8')))
    expect(moved.parameters.find(p => p.id === 'global.ring.tone')!.number).toBe(6)
    expect(moved.sourceRevision).toContain('changed-copy')

    const m = await withRegistry(moved)
    expect(m.registry.getParameter('global.ring.tone').number).toBe(6)
    expect(m.registry.findParametersForMessage('cc', 6, 16, { remoteTracks: 15, remoteGlobal: 16 }).map(p => p.id)).toEqual(['global.ring.tone'])
    expect(m.registry.findParametersForMessage('cc', 5, 16, { remoteTracks: 15, remoteGlobal: 16 })).toEqual([])

    const engine = m.context.createElixirMappingEngine(), template = m.templates.generateTemplate('effects')
    const mapping = engine.resolve(template)
    const a = mapping.assignments.find(a => a.parameter.id === 'global.ring.tone')!
    expect([a.messageType, a.channel, a.number]).toEqual(['cc', 16, 6])

    const file = m.codec.decodeFile(engine.exportFile(template, m.adapter.XL3_FILE_SERIALIZER)).mode
    const ccs = file.controls.filter(c => c.type === 'cc' && c.channel === 16).map(c => c.number)
    expect(ccs).toContain(6)
    expect(ccs).not.toContain(5)

    expect(m.monitor.createControllerSimulator(mapping).emit(a.control.id, 'value', 100)).toEqual([0xbf, 6, 100])

    vi.stubGlobal('ResizeObserver', class { observe() {} disconnect() {} })
    const w = mount(m.admin)
    await w.find('input[type=search]').setValue('ring tone'); await flushPromises()
    expect(w.text()).toContain('Ring Tone')
    expect(w.text()).toMatch(/Ring Tone [^·]*· Global · ch 16 · cc 6(?!\d)/)
    w.unmount(); vi.unstubAllGlobals()
  })

  it('writes the moved CC into the manual\'s and the cheat sheet\'s tables', () => {
    const src = firmwareCopy(moveRingTone)
    const script = `import sys, json; sys.path.insert(0, ${JSON.stringify(join(ELIXIR, 'tools'))})
import remote_params, remote_docs
from pathlib import Path
m = remote_docs.Map(remote_params.build(Path(${JSON.stringify(src)})))
print(json.dumps({**remote_docs.manual_blocks(m), **remote_docs.cheat_blocks(m)}))`
    const blocks = JSON.parse(execFileSync(PY, ['-c', script], { encoding: 'utf8' })) as Record<string, string[]>
    expect(blocks['manual-global-off-grid']!.join('\n')).toContain('| **6** | RING Tone |')
    expect(blocks['cheat-global-off-grid']!.join('\n')).toContain('<td><b>6</b>Tone</td>')
    expect(blocks['cheat-global-off-grid']!.join('\n')).not.toContain('<b>5</b>')
  })

  it('makes a removed parameter an explicit error, not a stale mapping', async () => {
    const doc = parseParameterDocument(JSON.parse(current.toString('utf8')))
    const m = await withRegistry({ ...doc, parameters: doc.parameters.filter(p => p.id !== 'global.ring.tone') })
    expect(() => m.registry.getParameter('global.ring.tone')).toThrow('Unknown Elixir parameter')
    expect(() => m.templates.generateTemplate('effects')).toThrow(/ring\.tone/)
    const saved = { schemaVersion: 1 as const, id: 'mine', name: 'Mine', deviceProfileId: 'novation.launch-control-xl3', channels: { remoteTracks: 15, remoteGlobal: 16 },
      receiver: { encoderMode: 'pickup' as const, buttonMode: 'press' as const }, assignments: [{ controlId: 'encoder.1.1', parameterId: 'global.ring.tone' }] }
    expect(m.context.createElixirMappingEngine().validate(saved).valid).toBe(false)
  })

  it('stops the build on a firmware symbol it cannot name', () => {
    const r = generate(firmwareCopy(s => s.replace('PAR_BPM, PAR_SHUFFLE,', 'PAR_NEW_THING, PAR_SHUFFLE,')))
    expect(r.status).not.toBe(0)
    expect(r.stderr).toContain("RM_GK[0] is 'PAR_NEW_THING'")
  })

  it('stops the build when two parameters land on one address', () => {
    const r = generate(firmwareCopy(s => s.replace('RM_XK[RM_NXK] = {3, 2, 4, 5, 7,', 'RM_XK[RM_NXK] = {3, 2, 4, 5, 8,')))
    expect(r.status).not.toBe(0)
    expect(r.stderr).toContain('the same address')
  })
})

describe('the registry document checks', () => {
  const doc = () => JSON.parse(JSON.stringify(parseParameterDocument(JSON.parse(readFileSync(join(ELIXIR, 'build/params/elixir-midi-parameters.json'), 'utf8')))))
  it('rejects numeric strings, missing fields, unknown fields and bad enums', () => {
    const cases: [(p: Record<string, unknown>) => void, string][] = [
      [p => { p.number = '8' }, 'number must be an integer'],
      [p => { delete p.minimum }, 'missing minimum'],
      [p => { p.extra = 1 }, 'unknown field extra'],
      [p => { p.scope = 'remote' }, 'bad scope'],
      [p => { p.channelGroup = 'remoteGlobal' }, 'channelGroup'],
      [p => { p.documentation = { source: 'reference/elixir_cheatsheet.pdf', page: 6 } }, 'firmware source'],
      [p => { p.name = '' }, 'name must be'],
    ]
    for (const [edit, message] of cases) {
      const d = doc(); edit(d.parameters.find((p: { id: string }) => p.id === 'tracks.d1.volume'))
      expect(() => parseParameterDocument(d), message).toThrow(message)
    }
    const d = doc(); d.parameters.push({ ...d.parameters[0] })
    expect(() => parseParameterDocument(d)).toThrow('id appears twice')
    const z = doc(); z.parameters.find((p: { id: string }) => p.id === 'tracks.pattern.1').number = 0
    expect(parseParameterDocument(z).parameters.find(p => p.id === 'tracks.pattern.1')!.number).toBe(0)   // (0 is a value)
  })
  it('keeps Unicode, quotes and line breaks', () => {
    const d = doc(); const p = d.parameters.find((p: { id: string }) => p.id === 'tracks.d1.volume')
    p.name = 'Volume “D1” — 音量\nline two'
    expect(parseParameterDocument(JSON.parse(JSON.stringify(d))).parameters.find(p => p.id === 'tracks.d1.volume')!.name).toBe(p.name)
  })
})
