import { mount, flushPromises } from '@vue/test-utils'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import MidiMapper from '../components/MidiMapper.vue'
import { PARAMETER_REGISTRY, XL3_PROFILE, LAUNCHPAD_PRO_MK3_PROFILE } from '../integration'
beforeEach(() => vi.stubGlobal('ResizeObserver', class { observe() {} disconnect() {} }))
const wrappers: ReturnType<typeof mount>[] = []
function setup(options = {}) { const w = mount(MidiMapper, { props: { options } }); wrappers.push(w); return w }
afterEach(() => { wrappers.forEach(w => w.unmount()); wrappers.length = 0; vi.unstubAllGlobals() })
it('shows a compact workspace, grouped parameters and channels without advanced panels', async () => {
  const request = vi.fn(); vi.stubGlobal('navigator', { requestMIDIAccess: request })
  const w = setup()
  expect(w.findAll('.control')).toHaveLength(48)
  expect(w.find('.mx-channels').exists()).toBe(false)   // (on the Setup tab: set once)
  expect(w.findAll('[role=tab]').map(t => t.text())).toEqual(['Tracks', 'Global', 'Control', 'Setup'])
  expect(w.findAll('.mx-group h2').map(h => h.text())).toEqual([expect.stringContaining('Knobs & sliders'), expect.stringContaining('Buttons & pads')])
  expect(w.find('.hardware-panel').exists()).toBe(false)
  expect(w.find('h1').exists()).toBe(false)
  expect(w.findAll('.control')[31]!.text()).toContain('Master volume')
  expect(w.text()).not.toContain('Selected track')
  await w.find('[role=tab]').trigger('keydown', { key: 'ArrowRight' }); await flushPromises()
  expect(w.find('[aria-selected=true]').text()).toBe('Global')
  expect(w.text()).toContain('Master filter')
  await w.find('[aria-selected=true]').trigger('keydown', { key: 'ArrowRight' }); await flushPromises()
  expect(w.find('[aria-selected=true]').text()).toBe('Control')
  expect(w.find('a[href="?view=admin"]').exists()).toBe(false)   // (the admin view stays in the code, unlinked)
  expect(request).not.toHaveBeenCalled()
})
it('supports category-only browsing, click assignment, bulk drop, colors and atomic rejection', async () => {
  const w = setup()
  await w.find('.mx-category select').setValue('Sends')
  expect(w.findAll('.mx-param').map(p => p.text())).toEqual([expect.stringContaining('Delay send'), expect.stringContaining('FX send'), expect.stringContaining('Reverb send')])
  await w.findAll('.mx-param')[1]!.trigger('click')
  await w.find('.control').trigger('click')
  expect(w.findAll('.control')[6]!.text()).toContain('OH FX send')
  expect(w.findAll('.control')[7]!.text()).toContain('Master volume')
  expect(w.emitted('template-change')).toBeTruthy()
  await w.findAll('.control')[32]!.trigger('drop', { dataTransfer: { getData: () => 'tracks.d1.fx-send' } })
  expect(w.find('[role=alert]').text()).toContain('continuous')
  expect(w.findAll('.control')[32]!.text()).toContain('D1 Mute')
  await w.find('[role=tab]#mapper-tab-control').trigger('click')
  await w.find('[aria-label="LED color: Orange"]').trigger('click')
  expect(w.find('.control').attributes('style')).toContain('#f5ad66')
  const emitted = w.emitted('template-change')!.at(-1)![0] as { name: string }
  emitted.name = 'Host mutation'
  expect((w.emitted('template-change')!.at(-1)![0] as { name: string }).name).toBe('Host mutation')
  // Mutating the delivered copy does not rename the workspace.
  await w.find('#mapper-tab-setup').trigger('click'); await flushPromises()
  await w.find('.mx-channels input').setValue('14')
  expect((w.emitted('template-change')!.at(-1)![0] as { name: string }).name).not.toBe('Host mutation')
  await w.find('.mx-devices button:last-child').trigger('click')
  expect(w.findAll('.control')).toHaveLength(64)
})
it('honors host devices, channels, registry and saved templates', async () => {
  const p = PARAMETER_REGISTRY.find(p => p.id === 'tracks.d1.volume')!
  const w = setup({ availableDevices: [XL3_PROFILE.id], defaultChannels: { remoteTracks: 13, remoteGlobal: 14 }, parameterOverrides: [{ ...p, name: 'Host volume', shortName: 'Host volume', number: 7 }] })
  expect(w.findAll('.mx-devices button')).toHaveLength(1)
  await w.find('#mapper-tab-setup').trigger('click'); await flushPromises()
  expect(w.find<HTMLInputElement>('.mx-channels input').element.value).toBe('13')
  expect(w.text()).toContain('Host volume')
  const pad = setup({ availableDevices: [LAUNCHPAD_PRO_MK3_PROFILE.id] })
  expect(pad.findAll('.control')).toHaveLength(64)
})

it('saves and reopens editable JSON from the toolbar and emits detached saves', async () => {
  const w = setup()
  let blob: Blob | undefined
  vi.stubGlobal('URL', { createObjectURL: (b: Blob) => { blob = b; return 'blob:test' }, revokeObjectURL: vi.fn() })
  const click = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {})
  try {
    await w.find('.mx-menu>button').trigger('click')
    await w.findAll('#mapper-download-options button')[1]!.trigger('click')
    const reader = new FileReader()
    const text = await new Promise<string>((resolve, reject) => { reader.onload = () => resolve(reader.result as string); reader.onerror = reject; reader.readAsText(blob!) })
    const saved = JSON.parse(text)
    expect(saved.channels.remoteTracks).toBe(15)
    expect(w.emitted('save-template')).toHaveLength(1)
    const input = w.find('input[type=file]')
    saved.channels.remoteTracks = 12
    Object.defineProperty(input.element, 'files', { configurable: true, value: [{ size: text.length, text: async () => JSON.stringify(saved) }] })
    await input.trigger('change'); await flushPromises()
    await w.find('#mapper-tab-setup').trigger('click'); await flushPromises()
    expect(w.find<HTMLInputElement>('.mx-channels input').element.value).toBe('12')
    expect(click).toHaveBeenCalledOnce()
  } finally { click.mockRestore() }
})

it('opens a guarded send panel with device-specific mode counts and preserves it on close', async () => {
  const request = vi.fn()
  vi.stubGlobal('navigator', { requestMIDIAccess: request })
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', { configurable: true, value: function(this: HTMLDialogElement) { this.open = true } })
  Object.defineProperty(HTMLDialogElement.prototype, 'close', { configurable: true, value: function(this: HTMLDialogElement) { this.open = false } })
  try {
    const w = setup()
    await w.find('.mx-primary').trigger('click'); await flushPromises()
    expect(w.find('dialog').attributes('open')).toBeDefined()
    expect(w.findAll('[aria-label="Destination custom mode"] option')).toHaveLength(15)
    expect(w.find('.monitor-panel').exists()).toBe(false)
    expect(request).not.toHaveBeenCalled()
    const selector = w.find('[aria-label="Destination custom mode"]')
    await selector.setValue('2')
    const verify = w.findAll('.hardware-panel button').find(b => b.text() === 'Verify device & back up slot 1')!
    expect(verify.attributes('disabled')).toBeDefined()
    const hardware = w.findComponent({ name: 'HardwareConnection' })
    hardware.vm.$emit('busy', true); await flushPromises()
    await w.find('[aria-label="Close send panel"]').trigger('click')
    expect(w.find('dialog').attributes('open')).toBeDefined()
    hardware.vm.$emit('busy', false); await flushPromises()
    await w.find('[aria-label="Close send panel"]').trigger('click')
    expect(w.find('dialog').attributes('open')).toBeUndefined()
    expect(w.findComponent({ name: 'HardwareConnection' }).exists()).toBe(true)
    await w.find('.mx-primary').trigger('click'); await flushPromises()
    expect(w.find<HTMLSelectElement>('[aria-label="Destination custom mode"]').element.value).toBe('2')
    await w.find('[aria-label="Close send panel"]').trigger('click')
    await w.find('.mx-devices button:last-child').trigger('click')
    await w.find('.mx-primary').trigger('click'); await flushPromises()
    expect(w.findAll('[aria-label="Destination custom mode"] option')).toHaveLength(8)
    await w.find('[aria-label="Destination custom mode"]').setValue('8')
    expect(w.findComponent({ name: 'LaunchpadTransfer' }).exists()).toBe(false)
    expect(w.text()).toContain('This app cannot send to the selected custom mode yet')
    expect(request).not.toHaveBeenCalled()
  } finally { Reflect.deleteProperty(HTMLDialogElement.prototype, 'showModal'); Reflect.deleteProperty(HTMLDialogElement.prototype, 'close') }
})
it('offers device and editable downloads and rejects non-config imports atomically', async () => {
  const w = setup()
  await w.find('.mx-menu>button').trigger('click')
  expect(w.find('#mapper-download-options').text()).toContain('Save device file (.syx)')
  expect(w.find('#mapper-download-options').text()).toContain('Save editable config (.json)')
  const input = w.find('[aria-label="Import configuration JSON"]')
  Object.defineProperty(input.element, 'files', { configurable: true, value: [{ name: 'mode.syx', size: 20, text: async () => 'bad' }] })
  await input.trigger('change'); await flushPromises()
  expect(w.find('[role=alert]').text()).toContain('Import an editable mapper JSON file')
  expect(w.findAll('.control')).toHaveLength(48)
  expect(w.findAll('.control')[31]!.text()).toContain('Master volume')
})
it('selects several controls and sets their colors together', async () => {
  const w = setup()
  const controls = () => w.findAll('.control')
  // Shift-click: the rectangle from the focused control (encoder 1/1) to fader 3: three rows of knobs and the faders
  await controls()[0]!.trigger('click'); await controls().find(c => c.attributes('data-control') === 'fader.3')!.trigger('click', { shiftKey: true })
  expect(w.findAll('.control[aria-pressed=true]').map(c => c.attributes('data-control'))).toEqual(
    ['encoder.1.1', 'encoder.1.2', 'encoder.1.3', 'encoder.2.1', 'encoder.2.2', 'encoder.2.3', 'encoder.3.1', 'encoder.3.2', 'encoder.3.3', 'fader.1', 'fader.2', 'fader.3'])
  expect(w.find('.mx-banner').text()).toContain('12 controls selected')
  await w.find('.mx-banner button').trigger('click'); await flushPromises()
  expect(w.find('.mx-title').text()).toContain('12 controls selected')
  const red = w.findAll('.mx-colors')[0]!.findAll('button').find(b => b.attributes('aria-label')?.endsWith('Red'))!
  const emitted = (w.emitted('template-change') ?? []).length
  await red.trigger('click'); await flushPromises()
  const t = (w.emitted('template-change') as [import('../core/mapping-engine/types').ControllerTemplate][]).at(-1)![0]
  expect((w.emitted('template-change') ?? []).length).toBeGreaterThan(emitted)
  const colored = t.assignments.filter(a => a.colors?.off !== undefined).map(a => a.controlId).sort()
  expect(colored).toEqual(['encoder.1.1', 'encoder.1.2', 'encoder.1.3', 'encoder.2.1', 'encoder.2.2', 'encoder.2.3', 'encoder.3.1', 'encoder.3.2', 'encoder.3.3'])   // (faders have no LED)
  expect(new Set(t.assignments.filter(a => a.colors).map(a => a.colors!.off)).size).toBe(1)
  // Cmd/Ctrl-click takes one out; a plain click selects one again
  await controls()[0]!.trigger('click', { metaKey: true })
  expect(w.findAll('.control[aria-pressed=true]')).toHaveLength(11)
  await controls()[5]!.trigger('click')
  expect(w.findAll('.control[aria-pressed=true]')).toHaveLength(1)
  // Clear on a selection clears them all
  await controls()[0]!.trigger('click'); await controls()[7]!.trigger('click', { shiftKey: true })
  await w.find('.mx-clear').trigger('click'); await flushPromises()
  expect(w.find('.mx-count').text()).toContain('36 of 48')   // (43 less row 1's seven: its 8th was empty)
})
it('walks through the FM-1 setup with the template\'s own channels', async () => {
  const w = setup({ parameterJsonUrl: '../data/elixir-midi-parameters.json' })
  await w.find('#mapper-tab-setup').trigger('click'); await flushPromises()
  await w.find('.mx-channels input').setValue('12')
  expect(w.find('.mx-steps').text()).toContain('Set Tracks to 12 and Global to 16')
  expect(w.find('a[href="../manual/#the-remote-map"]').exists()).toBe(true)
  expect(w.find('a[href="../data/elixir-midi-parameters.csv"]').exists()).toBe(true)
})
it('reads what the controller sends back in Elixir\'s words, sending nothing', async () => {
  const { default: ControllerCheck } = await import('../components/ControllerCheck.vue')
  const { generateTemplate } = await import('../elixir/templates')
  const w = mount(ControllerCheck, { props: { template: generateTemplate('mixer') } })
  await w.setProps({ incoming: { inputId: 'in', timestamp: 1, bytes: [0xbe, 72, 64] } })
  await w.setProps({ incoming: { inputId: 'in', timestamp: 2, bytes: [0xf8] } })            // (clock: ignored)
  await w.setProps({ incoming: { inputId: 'in', timestamp: 3, bytes: [0xbf, 8, 10] } })     // (GLOBAL's tempo: not on the mixer)
  const lines = w.findAll('.check-log li').map(l => l.text())
  expect(lines).toHaveLength(2)
  expect(lines[1]).toContain('Encoder 1/1 → D1 Reverb send')
  expect(lines[0]).toContain('Elixir: Tempo (not on this template)')
  // the guided rows: the first control of each, ticked by its own message, the next one named
  expect(w.findAll('.check-steps li').map(l => l.text().replace(/\s+/g, ' '))).toEqual([
    expect.stringContaining('✓Turn Encoder 1/1'), expect.stringContaining('Turn Encoder 2/1'), expect.stringContaining('Turn Encoder 3/1'),
    expect.stringContaining('Move Fader 1'), expect.stringContaining('Press Button 1/1'), expect.stringContaining('Press Button 2/1')])
  expect(w.text()).toContain('Next: turn Encoder 2/1.')
  w.unmount()
})
