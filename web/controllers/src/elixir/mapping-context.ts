import { createMappingEngine } from '../core/mapping-engine/engine'
import type { ControllerTemplate } from '../core/mapping-engine/types'
import { LAUNCHPAD_PRO_MK3_PROFILE } from '../devices/novation/launchpad-pro-mk3/profile'
import { XL3_PROFILE } from '../devices/novation/launch-control-xl3/profile'
import { LCXL_MK2_PROFILE } from '../devices/novation/launch-control-xl-mk2/profile'
import { channelWarnings, DEFAULT_CHANNEL_SETTINGS, validateChannelSettings, type OtherChannels } from './channel-settings'
import { PARAMETER_REGISTRY, type Parameter } from './parameter-registry'
/** Thin application composition boundary; the core accepts any compatible registry/profile. */
export function createElixirMappingEngine(otherChannels?: OtherChannels, parameters: readonly Parameter[] = PARAMETER_REGISTRY) {
  return createMappingEngine({ parameters, profiles: [XL3_PROFILE, LCXL_MK2_PROFILE, LAUNCHPAD_PRO_MK3_PROFILE], additionalValidation(template) {
    const channels = { remoteTracks: template.channels.remoteTracks, remoteGlobal: template.channels.remoteGlobal }
    try { validateChannelSettings(channels) } catch {
      return [{ severity: 'error', code: 'elixir-channels', message: 'Configure both Remote Tracks and Remote Global channels (1–16).' }]
    }
    return channelWarnings(channels, otherChannels).filter(w => w.code !== 'remote-overlap').map(w => ({ severity: 'warning' as const, code: w.code, message: w.message }))
  } })
}
export function createEmptyTemplate(id: string, name: string, deviceProfileId = XL3_PROFILE.id): ControllerTemplate {
  return { schemaVersion: 1, id, name, deviceProfileId, channels: { ...DEFAULT_CHANNEL_SETTINGS },
    receiver: { encoderMode: 'pickup', buttonMode: 'press' }, assignments: [] }
}
