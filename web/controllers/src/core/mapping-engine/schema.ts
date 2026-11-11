import type { ControllerTemplate } from './types'
const messageTypes = ['cc', 'note', 'program-change', 'realtime', 'nrpn']
function object(value: unknown, keys: readonly string[], required: readonly string[], path: string): asserts value is Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error(`${path}: expected object`)
  const record = value as Record<string, unknown>
  for (const k of Object.keys(record)) if (!keys.includes(k)) throw new Error(`${path}: unknown field ${k}`)
  for (const k of required) if (!Object.hasOwn(record, k)) throw new Error(`${path}: missing ${k}`)
}
function string(v: unknown, field: string) { if (typeof v !== 'string' || v.length > 1000) throw new Error(`${field}: expected string, at most 1000 characters`) }
function integer(v: unknown, min: number, max: number, field: string) { if (typeof v !== 'number' || !Number.isInteger(v) || v < min || v > max) throw new Error(`${field}: expected integer ${min}–${max}`) }
function choice(v: unknown, choices: readonly string[], field: string) { if (typeof v !== 'string' || !choices.includes(v)) throw new Error(`${field}: invalid choice`) }
/** Reject unknown fields and oversized data; never silently lose future schema settings. */
export function assertTemplate(value: unknown): asserts value is ControllerTemplate {
  const fields = ['schemaVersion', 'id', 'name', 'deviceProfileId', 'channels', 'receiver', 'destinationSlot', 'assignments']
  object(value, fields, fields.filter(k => k !== 'destinationSlot'), 'template')
  if (value.schemaVersion !== 1) throw new Error('Unsupported template schemaVersion')
  for (const k of ['id', 'name', 'deviceProfileId']) { string(value[k], k); if (!(value[k] as string).trim()) throw new Error(`${k}: cannot be empty`) }
  if (!value.channels || typeof value.channels !== 'object' || Array.isArray(value.channels)) throw new Error('channels: expected object')
  if (Object.keys(value.channels).length > 64) throw new Error('Too many channel groups')
  for (const [key, channel] of Object.entries(value.channels)) {
    if (!/^[a-zA-Z][a-zA-Z0-9_-]{0,63}$/.test(key) || ['constructor', 'prototype', '__proto__'].includes(key)) throw new Error('Invalid channel group')
    integer(channel, 1, 16, `channels.${key}`)
  }
  object(value.receiver, ['encoderMode', 'buttonMode'], ['encoderMode', 'buttonMode'], 'receiver')
  choice(value.receiver.encoderMode, ['pickup', 'absolute', 'relative'], 'receiver.encoderMode')
  choice(value.receiver.buttonMode, ['press', 'state'], 'receiver.buttonMode')
  if (Object.hasOwn(value, 'destinationSlot')) integer(value.destinationSlot, 1, 128, 'destinationSlot')
  if (!Array.isArray(value.assignments) || value.assignments.length > 1024) throw new Error('assignments: expected array, at most 1024 items')
  for (const a of value.assignments) {
    object(a, ['controlId', 'parameterId', 'messageType', 'channelOverride', 'range', 'label', 'encoderMode', 'buttonMode', 'colors'], ['controlId', 'parameterId'], 'assignment')
    for (const k of ['controlId', 'parameterId']) { string(a[k], k); if (!(a[k] as string).trim()) throw new Error(`${k}: cannot be empty`) }
    if (Object.hasOwn(a, 'messageType')) choice(a.messageType, messageTypes, 'messageType')
    if (Object.hasOwn(a, 'channelOverride')) integer(a.channelOverride, 1, 16, 'channelOverride')
    if (Object.hasOwn(a, 'range')) {
      object(a.range, ['minimum', 'maximum'], ['minimum', 'maximum'], 'range')
      integer(a.range.minimum, 0, 16383, 'range.minimum'); integer(a.range.maximum, 0, 16383, 'range.maximum')
      if ((a.range.minimum as number) > (a.range.maximum as number)) throw new Error('range: minimum exceeds maximum')
    }
    if (Object.hasOwn(a, 'label')) string(a.label, 'label')
    if (Object.hasOwn(a, 'encoderMode')) choice(a.encoderMode, ['absolute', 'relative'], 'encoderMode')
    if (Object.hasOwn(a, 'buttonMode')) choice(a.buttonMode, ['momentary', 'toggle'], 'buttonMode')
    if (Object.hasOwn(a, 'colors')) {
      object(a.colors, ['off', 'on'], [], 'colors')
      for (const k of Object.keys(a.colors)) integer(a.colors[k], 0, 127, `colors.${k}`)
    }
  }
}
export function importTemplate(json: string): ControllerTemplate {
  if (typeof json !== 'string' || json.length > 1_000_000) throw new Error('Template JSON exceeds maximum size')
  const value: unknown = JSON.parse(json)
  // Elixir's parameter list (elixir-midi-parameters.json) is the remote map, not a controller configuration.
  if (value && typeof value === 'object' && 'parameters' in value && 'firmwareVersion' in value)
    throw new Error("This is Elixir's parameter list, not a controller configuration. Open a file saved with Save JSON.")
  assertTemplate(value)
  return value
}
export function exportTemplate(template: ControllerTemplate): string {
  assertTemplate(template)
  return JSON.stringify(template, null, 2) + '\n'
}
