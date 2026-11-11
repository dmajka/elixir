import { mount } from '@vue/test-utils'
import { expect, it } from 'vitest'
import { AdminMidiMapper as MidiMapper, XL3_PROFILE, LAUNCHPAD_PRO_MK3_PROFILE, PARAMETER_REGISTRY } from '../integration'
import { generateTemplate } from '../elixir/templates'
it('shares host registry overrides with editor and simulator and emits detached persistence snapshots', async () => {
  const parameter = PARAMETER_REGISTRY.find(p => p.id === 'tracks.d1.volume')!
  const saved = generateTemplate('mixer')
  const w = mount(MidiMapper, { props: { options: {
    parameterOverrides: [{ ...parameter, name: 'Host volume', number: 7 }],
    defaultChannels: { remoteTracks: 13, remoteGlobal: 14 },
    availableDevices: [XL3_PROFILE.id], savedTemplates: [saved],
    theme: { className: 'host-theme', accent: '#ffaa00' },
  } } })
  try {
    expect(w.findAll('select[aria-label="Controller"] option')).toHaveLength(1)
    await w.find('.control.fader').trigger('click')
    expect(w.find('.editor').text()).toContain('Host volume · cc 7 · channel 13')
    expect(w.find('.mapper').classes()).toContain('host-theme')
    expect(w.find('.parameter-results').text()).toContain('Host volume')
    expect(w.find('.monitor-panel').text()).toContain('Host volume')
    await w.findAll('.export-panel button')[1]!.trigger('click')
    const event = w.emitted('save-template')![0]![0] as typeof saved
    expect(event.channels.remoteTracks).toBe(13)
    event.name = 'Mutated by host'
    expect(w.find<HTMLInputElement>('.setup input').element.value).not.toBe('Mutated by host')
    expect(saved).toEqual(generateTemplate('mixer'))
    await w.find('.setup input').setValue('Changed')
    expect(w.emitted('template-change')).toBeTruthy()
  } finally { w.unmount() }
})
it('starts on the first enabled device', () => {
  const w = mount(MidiMapper, { props: { options: { availableDevices: [LAUNCHPAD_PRO_MK3_PROFILE.id] } } })
  try { expect(w.findAll('.control')).toHaveLength(64); expect(w.text()).toContain('Mapping is valid.') } finally { w.unmount() }
})
