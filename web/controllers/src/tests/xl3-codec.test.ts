// @vitest-environment node
import { readFileSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { createDefaultMode, decodeFile, encodeFile } from '../devices/novation/launch-control-xl3/codec'
const fixture = (name: string) => new Uint8Array(readFileSync(new URL(`./fixtures/xl3/${name}.syx`, import.meta.url)))
describe('Components XL3 compatibility', () => {
  it('independently produces the genuine default export byte for byte', () => {
    expect(encodeFile(createDefaultMode())).toEqual(fixture('baseline'))
  })
  it('matches the isolated CC and channel edits', () => {
    const mode = createDefaultMode()
    mode.controls[0]!.number = 74
    expect(encodeFile(mode)).toEqual(fixture('encoder-cc74'))
    mode.controls[0]!.number = 13; mode.controls[0]!.channel = 15
    expect(encodeFile(mode)).toEqual(fixture('channel15-verified'))
  })
  it('matches relative, label, color, toggle and note reference exports', () => {
    const probes = [
      ['encoder-relative', 0, { relative: true }],
      ['label-isolated', 0, { name: 'Reverb D1' }],
      ['encoder-blue', 0, { offColor: 41 }],
      ['button-toggle', 32, { behavior: 'toggle' }],
      ['button-note', 32, { type: 'note', number: 69 }],
    ] as const
    for (const [file, index, change] of probes) {
      const m = createDefaultMode(); Object.assign(m.controls[index]!, change)
      expect(encodeFile(m)).toEqual(fixture(file))
    }
  })
  it.each(['baseline', 'encoder-cc74', 'channel15-verified', 'encoder-relative', 'label-isolated', 'encoder-blue', 'button-toggle', 'button-note', 'encoder-label', 'independent-baseline', 'independent-mixed', 'components-reexport-mixed'])('round-trips %s', name => {
    const bytes = fixture(name), decoded = decodeFile(bytes)
    expect(decoded.destination).toBe(127)
    expect(encodeFile(decoded.mode)).toEqual(bytes)
  })
})
describe('strict codec boundaries', () => {
  it('preserves the independent file through Components import/re-export', () => {
    expect(fixture('independent-baseline')).toEqual(fixture('baseline'))
    expect(fixture('components-reexport-mixed')).toEqual(fixture('independent-mixed'))
    const { mode } = decodeFile(fixture('components-reexport-mixed'))
    expect(mode.name).toBe('Elixir codec')
    expect(mode.controls[0]).toMatchObject({ channel: 1, number: 0, relative: true })
    expect(mode.controls[24]).toMatchObject({ channel: 16, number: 127 })
    expect(mode.controls[32]).toMatchObject({ type: 'note', channel: 15, number: 104, behavior: 'toggle' })
    expect(mode.controls[33]).toMatchObject({ type: 'program', channel: 16, number: 31 })
    expect(mode.controls[47]!.type).toBe('off')
  })
  it('allows maximum-length labels and rejects out-of-range bytes', () => {
    const m = createDefaultMode('x'.repeat(16)); m.controls[0]!.name = 'x'.repeat(31)
    expect(decodeFile(encodeFile(m)).mode.name).toBe(m.name)
    for (const number of [-1, 128, 0.5, NaN]) {
      m.controls[0]!.number = number
      expect(() => encodeFile(m)).toThrow()
    }
  })
  it('rejects missing or reordered controls and unsupported relative faders', () => {
    const m = createDefaultMode(); m.controls.reverse()
    expect(() => encodeFile(m)).toThrow('in order')
    m.controls.reverse(); m.controls[24]!.relative = true
    expect(() => encodeFile(m)).toThrow('encoder')
    m.controls.pop()
    expect(() => encodeFile(m)).toThrow('in order')
  })
  it('rejects duplicate IDs, missing records, mismatched names and destinations', () => {
    const b = fixture('baseline'), second = b.indexOf(247) + 1
    for (const [offset, value] of [[42, 16], [second + 10, 1], [second + 13, 65]] as const) {
      const bad = b.slice(); bad[offset] = value
      expect(() => decodeFile(bad)).toThrow()
    }
    const missing = new Uint8Array([...b.slice(0, 29), ...b.slice(40)])
    expect(() => decodeFile(missing)).toThrow('Incomplete')
    const flag = b.slice(); flag[35] = 68
    expect(() => decodeFile(flag)).toThrow('resolution')
  })
  it('encodes channel 1/16, CC 0/127, note/toggle, program and off controls', () => {
    const mode = createDefaultMode('Elixir codec')
    Object.assign(mode.controls[0]!, { channel: 1, number: 0, name: 'Reverb', relative: true })
    Object.assign(mode.controls[24]!, { channel: 16, number: 127, name: 'Volume' })
    Object.assign(mode.controls[32]!, { type: 'note', channel: 15, number: 104, behavior: 'toggle' })
    Object.assign(mode.controls[33]!, { type: 'program', channel: 16, number: 31, behavior: 'trigger' })
    mode.controls[47]!.type = 'off'
    const bytes = encodeFile(mode), decoded = decodeFile(bytes)
    expect(encodeFile(decoded.mode)).toEqual(bytes)
    expect(decoded.mode.controls[0]!.channel).toBe(1)
    expect(decoded.mode.controls[24]!.channel).toBe(16)
  })
  it.each([0, 17, 1.5, NaN])('rejects invalid channel %s', channel => {
    const m = createDefaultMode(); m.controls[0]!.channel = channel
    expect(() => encodeFile(m)).toThrow('Channel')
  })
  it('rejects lossy names and unsupported mappings', () => {
    expect(() => encodeFile(createDefaultMode('é'))).toThrow('ASCII')
    expect(() => encodeFile(createDefaultMode('x'.repeat(17)))).toThrow('16')
    const m = createDefaultMode(); m.controls[0]!.name = 'x'.repeat(32)
    expect(() => encodeFile(m)).toThrow('31')
    m.controls[0]!.name = ''; m.controls[0]!.type = 'note'
    expect(() => encodeFile(m)).toThrow('buttons')
  })
  it('rejects truncated, foreign, malformed and duplicate frames', () => {
    const b = encodeFile(createDefaultMode())
    expect(() => decodeFile(b.slice(0, -1))).toThrow()
    const bad = b.slice(); bad[5] = 14
    expect(() => decodeFile(bad)).toThrow('header')
    const high = b.slice(); high[35] = 128
    expect(() => decodeFile(high)).toThrow('seven-bit')
    const firstEnd = b.indexOf(247) + 1
    expect(() => decodeFile(new Uint8Array([...b.slice(0, firstEnd), ...b.slice(0, firstEnd)]))).toThrow('duplicate surface')
    const tag = b.slice(); tag[11] = 31
    expect(() => decodeFile(tag)).toThrow()
    const flags = b.slice(); flags[35] = 127
    expect(() => decodeFile(flags)).toThrow()
  })
})
