/** All public channel numbers are human-readable 1–16. */
export interface ChannelSettings { remoteTracks: number; remoteGlobal: number }
export const DEFAULT_CHANNEL_SETTINGS: Readonly<ChannelSettings> = Object.freeze({ remoteTracks: 15, remoteGlobal: 16 })
/** Each track on its own channel (CONFIG: MIDI CHANNELS; 1–7 by default): what a pad's hit (tracks.d1.hit ..) is sent
 * on. A template carries these only once it uses a hit (withTrackChannels adds the defaults it lacks). */
export const TRACK_CHANNEL_DEFAULTS = Object.freeze({ trackD1: 1, trackD2: 2, trackD3: 3, trackSN: 4, trackCY: 5, trackCH: 6, trackOH: 7 })
export type TrackChannelGroup = keyof typeof TRACK_CHANNEL_DEFAULTS
export const TRACK_CHANNEL_GROUPS = Object.keys(TRACK_CHANNEL_DEFAULTS) as TrackChannelGroup[]
export const isTrackChannelGroup = (g: string | undefined): g is TrackChannelGroup => !!g && Object.hasOwn(TRACK_CHANNEL_DEFAULTS, g)
/** the channels with every track's, its default where it has none */
export function withTrackChannels<T extends object>(channels: T): T & Record<TrackChannelGroup, number> {
  return { ...TRACK_CHANNEL_DEFAULTS, ...channels }
}
export interface OtherChannels { tracks?: readonly number[]; global?: number }
export function assertChannel(channel: number): void {
  if (!Number.isInteger(channel) || channel < 1 || channel > 16) throw new Error('MIDI channel must be an integer from 1 to 16')
}
export function validateChannelSettings(settings: ChannelSettings): void {
  assertChannel(settings.remoteTracks); assertChannel(settings.remoteGlobal)
}
export interface ChannelWarning { code: 'remote-overlap' | 'track-overlap' | 'global-overlap'; group: keyof ChannelSettings; channel: number; message: string }
/** Pure advisory check. An empty explicit track list disables the default track overlap check. */
export function channelWarnings(settings: ChannelSettings, others: OtherChannels = {}): ChannelWarning[] {
  validateChannelSettings(settings)
  const tracks = others.tracks ?? [1, 2, 3, 4, 5, 6, 7], global = others.global ?? 10
  tracks.forEach(assertChannel); assertChannel(global)
  const warnings: ChannelWarning[] = []
  if (settings.remoteTracks === settings.remoteGlobal) warnings.push({ code: 'remote-overlap', group: 'remoteTracks', channel: settings.remoteTracks, message: 'Remote Tracks and Remote Global use the same channel; assignments may collide.' })
  for (const group of ['remoteTracks', 'remoteGlobal'] as const) {
    const channel = settings[group]
    if (tracks.includes(channel)) warnings.push({ code: 'track-overlap', group, channel, message: `${group} overlaps a conventional track channel (${channel}).` })
    if (channel === global) warnings.push({ code: 'global-overlap', group, channel, message: `${group} overlaps the normal global channel (${channel}).` })
  }
  return warnings
}
