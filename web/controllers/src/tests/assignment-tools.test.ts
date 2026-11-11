import { expect, it } from 'vitest'
import { assignTrackRow } from '../elixir/assignment-tools'
import { generateTemplate } from '../elixir/templates'
import { PARAMETER_REGISTRY } from '../elixir/parameter-registry'
import { XL3_PROFILE } from '../devices/novation/launch-control-xl3/profile'
import { LAUNCHPAD_PRO_MK3_PROFILE } from '../devices/novation/launchpad-pro-mk3/profile'
import { generateLaunchpadTemplate } from '../elixir/launchpad-templates'
import { createElixirMappingEngine } from '../elixir/mapping-context'
it('fills seven explicit track CCs, reserves master, preserves other rows and source', () => {
  const original = generateTemplate('mixer'), before = JSON.stringify(original)
  const next = assignTrackRow(original, XL3_PROFILE, PARAMETER_REGISTRY, 'encoder.1.1', 'tracks.d1.fx-send')
  const resolved = createElixirMappingEngine().resolve(next)
  expect(resolved.assignments.filter(a => a.control.group === 'encoders.1').map(a => [a.number, a.channel])).toEqual([...[64,65,66,67,68,69,70].map(n => [n,15]), [124,15]])
  expect(next.assignments.filter(a => !a.controlId.startsWith('encoder.1.'))).toEqual(original.assignments.filter(a => !a.controlId.startsWith('encoder.1.')))
  expect(JSON.stringify(original)).toBe(before)
})
it('fills pad actions without inventing a master mute and rejects incomplete families', () => {
  const original = generateLaunchpadTemplate('pad-mute-solo')
  const next = assignTrackRow(original, LAUNCHPAD_PRO_MK3_PROFILE, PARAMETER_REGISTRY, 'pad.1.1', 'tracks.d1.mute')
  expect(next.assignments.some(a => a.controlId === 'pad.1.8')).toBe(false)
  expect(createElixirMappingEngine().resolve(next).assignments.filter(a => a.control.row === 0).map(a => a.number)).toEqual([104,105,106,107,108,109,110])
  expect(() => assignTrackRow(original, LAUNCHPAD_PRO_MK3_PROFILE, [], 'pad.1.1', 'tracks.d1.mute')).toThrow()
})
