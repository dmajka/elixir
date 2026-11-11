/** Elixir's remote map: generated from the firmware on every build (tools/remote_params.py reads remote.c, midi.c ..),
 * checked (registry-schema.ts) and bundled by vite.config.ts as virtual:elixir-parameters. No hand-kept table: a CC
 * that moves in the firmware moves here, in the presets, the exports and the simulator alike.
 * No conventional voice CCs are included in this remote registry.
 */
import generated from 'virtual:elixir-parameters'
import { validateChannelSettings, type ChannelSettings } from './channel-settings'
import { parseParameterDocument } from './registry-schema'
export type MessageType = 'cc' | 'note' | 'program-change' | 'realtime'
export type Track = 'd1' | 'd2' | 'd3' | 'sn' | 'cy' | 'ch' | 'oh' | 'selected'
export interface Parameter {
  readonly id: string
  readonly name: string
  readonly shortName: string
  readonly category: string
  readonly scope: 'remote-tracks' | 'remote-global' | 'system'
  readonly track?: Track
  readonly channelGroup?: keyof ChannelSettings
  readonly messageType: MessageType
  readonly alternateMessageTypes: readonly MessageType[]
  /** CC/note/program number, or system real-time status byte. */
  readonly number: number
  readonly minimum: number
  readonly maximum: number
  readonly controlType: 'continuous' | 'button' | 'selection' | 'realtime'
  readonly interpretation: string
  readonly buttonBehavior?: 'press-or-state' | 'held' | 'press' | 'tap-latch-hold'
  readonly encoderBehavior?: readonly ['PICKUP', 'ABS', 'REL']
  readonly feedback?: { readonly source: 'USB'; readonly controllerCompatibility: 'unverified' }
  /** Where the firmware defines it: a file under firmware/src and the symbol (table entry, enum, #define). */
  readonly documentation: { readonly source: string; readonly symbol: string }
}
export const TRACKS = ['d1', 'd2', 'd3', 'sn', 'cy', 'ch', 'oh', 'selected'] as const
const document = parseParameterDocument(generated)
/** The firmware the registry was generated from (shown on the page; kept with the downloadable JSON). */
export const REGISTRY_SOURCE = Object.freeze({ firmwareVersion: document.firmwareVersion, sourceRevision: document.sourceRevision })
// Freeze nested metadata too: consumers must use explicit registry overrides later.
// The JSON is sorted by id; the mapper lists TRACKS, then GLOBAL, then system, each by message type and number.
const SCOPE_ORDER = ['remote-tracks', 'remote-global', 'system'], TYPE_ORDER = ['cc', 'note', 'program-change', 'realtime']
const displayOrder = (a: Parameter, b: Parameter) => SCOPE_ORDER.indexOf(a.scope) - SCOPE_ORDER.indexOf(b.scope) ||
  TYPE_ORDER.indexOf(a.messageType) - TYPE_ORDER.indexOf(b.messageType) || a.number - b.number
export const PARAMETER_REGISTRY: readonly Parameter[] = Object.freeze([...document.parameters].sort(displayOrder).map(p => Object.freeze({ ...p, alternateMessageTypes: Object.freeze([...p.alternateMessageTypes]), documentation: Object.freeze({ ...p.documentation }), ...(p.encoderBehavior ? { encoderBehavior: Object.freeze([...p.encoderBehavior]) as Parameter['encoderBehavior'] } : {}), ...(p.feedback ? { feedback: Object.freeze({ ...p.feedback }) } : {}) })))
const byId = new Map(PARAMETER_REGISTRY.map(p => [p.id, p]))
if (byId.size !== PARAMETER_REGISTRY.length) throw new Error('Duplicate parameter IDs')
export function getParameter(id: string): Parameter {
  const p = byId.get(id)
  if (!p) throw new Error(`Unknown Elixir parameter: ${id}`)
  return p
}
export function filterParameters(filter: { query?: string; track?: Track; category?: string; scope?: Parameter['scope']; messageType?: MessageType } = {}): Parameter[] {
  const query = filter.query?.trim().toLowerCase()
  return PARAMETER_REGISTRY.filter(p => (!query || `${p.id} ${p.name} ${p.category}`.toLowerCase().includes(query)) && (!filter.track || p.track === filter.track) && (!filter.category || p.category === filter.category) && (!filter.scope || p.scope === filter.scope) && (!filter.messageType || [p.messageType, ...p.alternateMessageTypes].includes(filter.messageType)))
}
export interface ResolveOptions { messageType?: MessageType; value?: number; release?: boolean }
/** Resolve and encode one event. No I/O, toggle state machine, or clock scheduler. */
export function resolveParameter(id: string, channels: ChannelSettings, options: ResolveOptions = {}) {
  validateChannelSettings(channels)
  const parameter = getParameter(id), messageType = options.messageType ?? parameter.messageType
  if (![parameter.messageType, ...parameter.alternateMessageTypes].includes(messageType)) throw new Error('Unsupported message type for parameter')
  if (options.release && parameter.controlType !== 'button') throw new Error('Only buttons have release events')
  const channel = parameter.channelGroup ? channels[parameter.channelGroup] : undefined
  const number = parameter.number
  if (messageType === 'program-change' || messageType === 'realtime') {
    if (options.value !== undefined) throw new Error('Value does not apply to program/real-time messages')
    return { parameter, messageType, channel, number, bytes: Uint8Array.from(messageType === 'realtime' ? [number] : [0xc0 | (channel! - 1), number]) }
  }
  if (options.release && options.value !== undefined) throw new Error('Release value is fixed at zero')
  const value = options.release ? 0 : options.value ?? parameter.maximum
  if (!Number.isInteger(value) || value < parameter.minimum || value > parameter.maximum) throw new Error(`Value must be ${parameter.minimum}–${parameter.maximum}`)
  const status = messageType === 'cc' ? 0xb0 : options.release ? 0x80 : 0x90
  return { parameter, messageType, channel, number, bytes: Uint8Array.from([status | (channel! - 1), number, value]) }
}
/** Return ALL matches; channel conflicts must never silently choose one namespace. */
export function findParametersForMessage(messageType: MessageType, number: number, channel: number | undefined, channels: ChannelSettings): Parameter[] {
  validateChannelSettings(channels)
  return PARAMETER_REGISTRY.filter(p => [p.messageType, ...p.alternateMessageTypes].includes(messageType) && p.number === number && (p.channelGroup ? channels[p.channelGroup] === channel : channel === undefined))
}
