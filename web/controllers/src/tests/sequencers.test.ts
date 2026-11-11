/** The Hapax's instrument definitions (tools/hapax_defs.py, written by web/make_site.py into data/hapax/): every line
 * as Squarp's template (https://squarp.net/hapax/instr_def/template.txt) allows it, and every number the remote map's,
 * from the same firmware. And the page's Hapax panel: its downloads, and the controller's toolbar out of the way. */
import { spawnSync } from 'node:child_process'
import { mkdtempSync, readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { flushPromises, mount } from '@vue/test-utils'
import { afterEach, beforeAll, describe, expect, it, vi } from 'vitest'
import MidiMapper from '../components/MidiMapper.vue'
import { PARAMETER_REGISTRY } from '../integration'

const ELIXIR = join(process.cwd(), '../..')
const NAME = /^[A-Za-z0-9 _+-]+$/
const SETTINGS: Record<string, RegExp> = {
  VERSION: /^1$/, TRACKNAME: /^(NULL|[A-Za-z0-9 _+-]+)$/, TYPE: /^(POLY|DRUM|MPE|POLYAT|AFTR|NULL)$/,
  OUTPORT: /^(NULL|A|B|C|D|USBD|USBH)$/, OUTCHAN: /^(NULL|[1-9]|1[0-6])$/, INPORT: /^(NULL|NONE|ALLACTIVE|A|B|USBH|USBD|CVG)$/,
  INCHAN: /^(NULL|ALL|[1-9]|1[0-6])$/, MAXRATE: /^(NULL|192|96|64|48|32|24|16|12|8|6|4|3|2|1)$/,
}
const SECTIONS = ['DRUMLANES', 'PC', 'CC', 'CC_PAIR', 'NRPN', 'ASSIGN', 'AUTOMATION', 'COMMENT']

function parse(text: string) {
  const settings: Record<string, string> = {}, sections: Record<string, string[]> = {}
  let open: string | undefined
  for (const raw of text.split('\n')) {
    const line = (open === 'COMMENT' ? raw : raw.replace(/#.*/, '')).trim()
    if (!line) continue
    const tag = /^\[(\/?)([A-Z_]+)\]$/.exec(line)
    if (tag) {
      expect(SECTIONS).toContain(tag[2])
      if (tag[1]) { expect(open).toBe(tag[2]); open = undefined } else { expect(open).toBeUndefined(); open = tag[2]!; sections[open] = [] }
      continue
    }
    if (open) { sections[open]!.push(line); continue }
    const [key, ...rest] = line.split(' ')
    expect(Object.keys(SETTINGS)).toContain(key)
    settings[key!] = rest.join(' ')
    expect(settings[key!]).toMatch(SETTINGS[key!]!)
  }
  expect(open).toBeUndefined()
  return { settings, sections }
}
const int = (s: string, lo: number, hi: number) => { expect(s).toMatch(/^\d+$/); const n = Number(s); expect(n).toBeGreaterThanOrEqual(lo); expect(n).toBeLessThanOrEqual(hi); return n }

describe('the Hapax instrument definitions', () => {
  let files: Record<string, ReturnType<typeof parse>>
  let raw: Record<string, string>
  beforeAll(() => {
    const out = mkdtempSync(join(tmpdir(), 'elixir-hapax-'))
    const r = spawnSync(process.env.PYTHON || 'python3', [join(ELIXIR, 'tools/hapax_defs.py'), out], { encoding: 'utf8' })
    expect(r.status, r.stderr).toBe(0)
    raw = Object.fromEntries(['ELIXIR_DRUMS.txt', 'ELIXIR_GLOBAL.txt'].map(f => [f, readFileSync(join(out, f), 'utf8')]))
    files = Object.fromEntries(Object.entries(raw).map(([f, t]) => [f, parse(t)]))
  })
  const byNumber = (group: string, type: string) => new Map(PARAMETER_REGISTRY.filter(p => p.channelGroup === group && p.messageType === type).map(p => [p.number, p]))

  it('are ASCII, VERSION 1 first, drum tracks on the remote channels', () => {
    for (const t of Object.values(raw)) { expect(/^[\x09\x0a\x20-\x7e]*$/.test(t)).toBe(true); expect(t.replace(/^(#.*\n|\n)*/, '')).toMatch(/^VERSION 1\n/) }
    expect(files['ELIXIR_DRUMS.txt']!.settings).toMatchObject({ TYPE: 'DRUM', OUTCHAN: '15', OUTPORT: 'NULL' })
    expect(files['ELIXIR_GLOBAL.txt']!.settings).toMatchObject({ TYPE: 'DRUM', OUTCHAN: '16', OUTPORT: 'NULL' })
  })

  it('play D1 to OH on the track channels at note 60', () => {
    expect(files['ELIXIR_DRUMS.txt']!.sections.DRUMLANES).toEqual(['1:NULL:1:60 D1', '2:NULL:2:60 D2', '3:NULL:3:60 D3', '4:NULL:4:60 SN', '5:NULL:5:60 CY', '6:NULL:6:60 CH', '7:NULL:7:60 OH'])
  })

  it('name the remote map\'s CCs, PCs, pots and lanes with its own numbers', () => {
    for (const [f, group] of [['ELIXIR_DRUMS.txt', 'remoteTracks'], ['ELIXIR_GLOBAL.txt', 'remoteGlobal']] as const) {
      const { sections } = files[f]!, ccs = byNumber(group, 'cc'), pcs = byNumber(group, 'program-change')
      const named = sections.CC!.map(l => { const [n, ...name] = l.split(' '); expect(name.join(' ')).toMatch(NAME); return int(n!, 0, 119) })
      expect(named).toEqual([...ccs.keys()].filter(n => n <= 119).sort((a, b) => a - b))
      expect(sections.PC!.map(l => int(l.split(' ')[0]!, 1, 128) - 1)).toEqual([...pcs.keys()].sort((a, b) => a - b))
      const pots = sections.ASSIGN!.map(l => /^([1-8]) CC:(\d+)$/.exec(l)!)
      expect(pots.map(m => Number(m[1]))).toEqual([1, 2, 3, 4, 5, 6, 7, 8])
      for (const m of pots) expect(ccs.get(Number(m[2]))?.controlType).toBe('continuous')
      for (const l of sections.DRUMLANES!) {
        const m = /^(\d+):NULL:(NULL|\d+):(\d+) ([A-Za-z0-9 _+-]+)$/.exec(l)!
        expect(m, l).toBeTruthy(); int(m[1]!, 1, 16); int(m[3]!, 0, 127)
        if (group === 'remoteGlobal') expect(ccs.get(Number(m[3]))?.buttonBehavior).toBe('held')   // (a note is the same button)
      }
      for (const l of sections.COMMENT!) expect(l).toMatch(NAME)
    }
  })
})

describe('the OXI One instrument definitions', () => {
  let files: Record<string, { id: string; name: string; abbr: string; manufacturer: string; parameters: Record<string, unknown>[]; script: null }>
  beforeAll(() => {
    const out = mkdtempSync(join(tmpdir(), 'elixir-oxi-'))
    const r = spawnSync(process.env.PYTHON || 'python3', [join(ELIXIR, 'tools/oxi_defs.py'), out], { encoding: 'utf8' })
    expect(r.status, r.stderr).toBe(0)
    files = Object.fromEntries(['ELIXIR_TRACKS.oxiindef', 'ELIXIR_GLOBAL.oxiindef'].map(f => [f, JSON.parse(readFileSync(join(out, f), 'utf8'))]))
  })
  it('are shaped as the OXI App\'s (MIDI Guide\'s exports), every CC up to 119 on their channel, each name once', () => {
    for (const [f, group] of [['ELIXIR_TRACKS.oxiindef', 'remoteTracks'], ['ELIXIR_GLOBAL.oxiindef', 'remoteGlobal']] as const) {
      const d = files[f]!
      expect(Object.keys(d).sort()).toEqual(['abbr', 'id', 'manufacturer', 'name', 'parameters', 'script'])
      expect(d.abbr).toMatch(/^[A-Za-z0-9-]{1,4}$/); expect(d.script).toBeNull()
      const want = PARAMETER_REGISTRY.filter(p => p.channelGroup === group && p.messageType === 'cc' && p.number <= 119).sort((a, b) => a.number - b.number)
      expect(d.parameters.map(p => p.nr1)).toEqual(want.map(p => p.number))
      for (const [i, p] of d.parameters.entries()) {
        expect(Object.keys(p).sort()).toEqual(['abbr', 'default_value', 'maximum', 'minimum', 'name', 'nr1', 'nr2', 'type'])
        expect(p).toMatchObject({ type: 'cc', nr2: 0, minimum: want[i]!.minimum, maximum: want[i]!.maximum })
        expect(p.abbr).toMatch(/^[A-Za-z0-9-]{1,4}$/)
        expect(p.default_value as number).toBeGreaterThanOrEqual(want[i]!.minimum); expect(p.default_value as number).toBeLessThanOrEqual(want[i]!.maximum)
      }
      expect(new Set(d.parameters.map(p => p.abbr)).size).toBe(d.parameters.length)
    }
    expect(files['ELIXIR_TRACKS.oxiindef']!.parameters.find(p => p.nr1 === 27)).toMatchObject({ name: 'SN Filter frequency', abbr: 'SNFl' })
  })
})

describe('the page\'s sequencers', () => {
  afterEach(() => { vi.unstubAllGlobals(); history.replaceState(null, '', '/') })
  const site = { parameterJsonUrl: '../data/elixir-midi-parameters.json' }
  function setup(options = {}) {
    vi.stubGlobal('ResizeObserver', class { observe() {} disconnect() {} })
    vi.stubGlobal('fetch', vi.fn(async (url: string) => ({ ok: true, text: async () => `file ${url}` })))
    return mount(MidiMapper, { props: { options } })
  }
  it('offers each one\'s files from data/, with the controller tools set aside', async () => {
    const w = setup(site)
    expect(w.findAll('[aria-label=Sequencer] button').map(b => b.text())).toEqual(['Hapax', 'OXI One'])
    await w.find('[aria-label=Sequencer] button').trigger('click'); await flushPromises()
    expect(location.hash).toBe('#hapax')
    expect(w.findAll('.hx-get').map(a => a.attributes('href'))).toEqual(['../data/hapax/ELIXIR_DRUMS.txt', '../data/hapax/ELIXIR_GLOBAL.txt'])
    expect(w.find('.mx-layout').exists()).toBe(false)
    expect(w.find('.mx-start').exists()).toBe(false)
    expect(w.find('.mx-files').exists()).toBe(false)
    expect(w.findAll('.hx-file pre').map(p => p.text())).toEqual(['file ../data/hapax/ELIXIR_DRUMS.txt', 'file ../data/hapax/ELIXIR_GLOBAL.txt'])
    await w.findAll('[aria-label=Sequencer] button')[1]!.trigger('click'); await flushPromises()
    expect(location.hash).toBe('#oxi')
    expect(w.findAll('.hx-get').map(a => a.attributes('href'))).toEqual(['../data/oxi/ELIXIR_TRACKS.oxiindef', '../data/oxi/ELIXIR_GLOBAL.oxiindef'])
    expect(w.find('.hx-steps').text()).toContain('15 for Tracks, 16 for Global')
    await w.find('[aria-label=Controller] button').trigger('click')
    expect(w.find('.mx-layout').exists()).toBe(true)
    expect(location.hash).toBe('')
    w.unmount()
  })
  it('opens from the address (#oxi), and lists everything in one menu for narrow screens', async () => {
    history.replaceState(null, '', '/#oxi')
    const w = setup(site)
    expect(w.find('.hx-title').text()).toBe('OXI One')
    const all = w.find<HTMLSelectElement>('.mx-device-select')
    expect(all.element.value).toBe('seq:oxi')
    expect(all.findAll('optgroup').map(g => g.attributes('label'))).toEqual(['Controllers', 'Sequencers'])
    await all.setValue(all.findAll('option')[1]!.element.value); await flushPromises()   // (the XL mk2)
    expect(w.find('.mx-layout').exists()).toBe(true)
    expect(w.find<HTMLSelectElement>('.mx-model').element.value).toBe(all.element.value)
    w.unmount()
  })
  it('hide without a site to download from', () => {
    history.replaceState(null, '', '/#hapax')
    const w = setup({})
    expect(w.find('[aria-label=Sequencer]').exists()).toBe(false)
    expect(w.find('.mx-layout').exists()).toBe(true)
    w.unmount()
  })
})
