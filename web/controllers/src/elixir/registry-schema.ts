/** The parameter-registry JSON (tools/remote_params.py: elixir-midi-parameters.json), checked field by field.
 * This is Elixir's remote map, generated from the firmware: NOT a controller configuration (schema.ts's templates).
 * The same rules as remote_params.py's validate(); the mapper's build (vite.config.ts) and tests use it. */
import type { Parameter } from './parameter-registry'

export const REGISTRY_SCHEMA_VERSION = 1
export interface ParameterDocument {
  readonly schemaVersion: 1
  readonly firmwareVersion: string
  readonly sourceRevision: string
  readonly parameters: readonly Parameter[]
}
const FIELDS = ['id', 'name', 'shortName', 'category', 'scope', 'track', 'channelGroup', 'messageType', 'alternateMessageTypes',
  'number', 'minimum', 'maximum', 'controlType', 'interpretation', 'buttonBehavior', 'encoderBehavior', 'feedback', 'documentation']
const REQUIRED = ['id', 'name', 'shortName', 'category', 'scope', 'messageType', 'alternateMessageTypes', 'number', 'minimum',
  'maximum', 'controlType', 'interpretation', 'documentation']
const TYPES = ['cc', 'note', 'program-change', 'realtime']
const GROUP: Record<string, string | undefined> = { 'remote-tracks': 'remoteTracks', 'remote-global': 'remoteGlobal', system: undefined }
const TRACKS = ['d1', 'd2', 'd3', 'sn', 'cy', 'ch', 'oh', 'selected']

function fail(where: string, what: string): never { throw new Error(`parameter registry: ${where}: ${what}`) }
const isObject = (v: unknown): v is Record<string, unknown> => !!v && typeof v === 'object' && !Array.isArray(v)
function text(p: Record<string, unknown>, k: string, where: string) {
  if (typeof p[k] !== 'string' || !(p[k] as string).trim()) fail(where, `${k} must be a nonempty string`)
}
function int(p: Record<string, unknown>, k: string, where: string) {
  if (typeof p[k] !== 'number' || !Number.isInteger(p[k])) fail(where, `${k} must be an integer (not ${JSON.stringify(p[k])})`)
  return p[k] as number
}

export function parseParameterDocument(value: unknown): ParameterDocument {
  if (!isObject(value)) fail('document', 'expected an object')
  for (const k of Object.keys(value)) if (!['schemaVersion', 'firmwareVersion', 'sourceRevision', 'parameters'].includes(k)) fail('document', `unknown field ${k}`)
  if (value.schemaVersion !== REGISTRY_SCHEMA_VERSION) fail('document', `schemaVersion ${JSON.stringify(value.schemaVersion)} unsupported`)
  text(value, 'firmwareVersion', 'document'); text(value, 'sourceRevision', 'document')
  if (!Array.isArray(value.parameters) || !value.parameters.length) fail('document', 'parameters must be a nonempty array')
  const ids = new Set<string>(), addresses = new Map<string, string>()
  value.parameters.forEach((p: unknown, i: number) => {
    let where = `parameters[${i}]`
    if (!isObject(p)) fail(where, 'expected an object')
    if (typeof p.id !== 'string' || !/^[a-z0-9]+(?:[.-][a-z0-9]+)*$/.test(p.id)) fail(where, `bad id ${JSON.stringify(p.id)}`)
    where = p.id
    if (ids.has(p.id)) fail(where, 'id appears twice')
    ids.add(p.id)
    for (const k of Object.keys(p)) if (!FIELDS.includes(k)) fail(where, `unknown field ${k}`)
    for (const k of REQUIRED) if (!Object.hasOwn(p, k)) fail(where, `missing ${k}`)
    for (const k of ['name', 'shortName', 'category', 'interpretation']) text(p, k, where)
    const number = int(p, 'number', where), min = int(p, 'minimum', where), max = int(p, 'maximum', where)
    if (!(p.scope as string in GROUP)) fail(where, `bad scope ${JSON.stringify(p.scope)}`)
    if (p.channelGroup !== GROUP[p.scope as string]) fail(where, 'channelGroup does not match scope')
    if (Object.hasOwn(p, 'track') && !TRACKS.includes(p.track as string)) fail(where, 'bad track')
    if (!TYPES.includes(p.messageType as string)) fail(where, 'bad messageType')
    if (!Array.isArray(p.alternateMessageTypes) || p.alternateMessageTypes.some(t => !TYPES.includes(t))) fail(where, 'bad alternateMessageTypes')
    if (!['continuous', 'button', 'selection', 'realtime'].includes(p.controlType as string)) fail(where, 'bad controlType')
    if (min < 0 || min > max || max > 127) fail(where, `bad range ${min}–${max}`)
    if (p.messageType === 'realtime' ? number < 0xf8 || number > 0xff : number < 0 || number > 127) fail(where, `number ${number} out of range`)
    if (p.messageType === 'program-change' && (number < min || number > max)) fail(where, 'program outside its range')
    if ((p.controlType === 'button') !== Object.hasOwn(p, 'buttonBehavior')) fail(where, 'buttonBehavior goes with buttons only')
    if (Object.hasOwn(p, 'buttonBehavior') && !['press-or-state', 'held', 'press', 'tap-latch-hold'].includes(p.buttonBehavior as string)) fail(where, 'bad buttonBehavior')
    if (Object.hasOwn(p, 'encoderBehavior') && JSON.stringify(p.encoderBehavior) !== '["PICKUP","ABS","REL"]') fail(where, 'bad encoderBehavior')
    if (Object.hasOwn(p, 'feedback') && JSON.stringify(p.feedback) !== '{"source":"USB","controllerCompatibility":"unverified"}') fail(where, 'bad feedback')
    if (!isObject(p.documentation) || typeof p.documentation.source !== 'string' || !p.documentation.source.startsWith('firmware/src/')) fail(where, 'documentation must name its firmware source')
    for (const type of [p.messageType, ...p.alternateMessageTypes] as string[]) {
      const address = `${p.channelGroup ?? 'none'}/${type}/${number}`, other = addresses.get(address)
      if (other) fail(where, `same address as ${other} (${address})`)
      addresses.set(address, p.id)
    }
  })
  return value as unknown as ParameterDocument
}
