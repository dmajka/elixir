import { assertTemplate } from './schema'
import type { ControllerTemplate, DeviceProfile, FileSerializer, MappingIssue, MappingParameter, MappingResult, ResolvedAssignment, TextConstraint } from './types'
export interface ValidationReport { valid: boolean; issues: MappingIssue[]; unassignedControlIds: string[] }
export class MappingValidationError extends Error {
  constructor(public readonly issues: readonly MappingIssue[]) {
    super(issues.filter(i => i.severity === 'error').map(i => i.message).join('; '))
    this.name = 'MappingValidationError'
  }
}
function validText(value: string, limit: TextConstraint) { return value.length <= limit.maximumLength && (!limit.ascii || /^[\x20-\x7d]*$/.test(value)) }
function defaultLabel(value: string, limit: TextConstraint) { return (limit.ascii ? value.replace(/[^\x20-\x7d]/g, ' ') : value).slice(0, limit.maximumLength) }
const channelValid = (v: unknown): v is number => typeof v === 'number' && Number.isInteger(v) && v >= 1 && v <= 16
/** Context is snapshotted once, so host edits cannot change an engine's validation rules. */
export function createMappingEngine(context: { parameters: readonly MappingParameter[]; profiles: readonly DeviceProfile[]; additionalValidation?: (template: ControllerTemplate) => readonly MappingIssue[] }) {
  const parameters = new Map<string, MappingParameter>(), profiles = new Map<string, DeviceProfile>()
  for (const p of structuredClone(context.parameters)) {
    const types = ['cc', 'note', 'program-change', 'realtime', 'nrpn']
    const bound = p.messageType === 'nrpn' ? 16383 : p.messageType === 'realtime' ? 255 : 127
    if (parameters.has(p.id) || !p.id || !types.includes(p.messageType) || p.alternateMessageTypes.some(t => !types.includes(t)) ||
      !Number.isInteger(p.number) || p.number < 0 || p.number > bound || !Number.isInteger(p.minimum) || !Number.isInteger(p.maximum) || p.minimum < 0 || p.minimum > p.maximum || p.maximum > (p.messageType === 'nrpn' ? 16383 : 127)) throw new Error('Invalid or duplicate registry parameter')
    parameters.set(p.id, p)
  }
  for (const p of structuredClone(context.profiles)) {
    if (profiles.has(p.id) || !p.id || new Set(p.controls.map(c => c.id)).size !== p.controls.length) throw new Error('Invalid or duplicate device profile/control IDs')
    profiles.set(p.id, p)
  }
  function analyze(input: unknown): { report: ValidationReport; result?: MappingResult } {
    const issues: MappingIssue[] = []
    const issue = (severity: MappingIssue['severity'], code: string, message: string, controlId?: string) => issues.push({ severity, code, message, ...(controlId ? { controlId } : {}) })
    try { assertTemplate(input) } catch (error) {
      return { report: { valid: false, issues: [{ severity: 'error', code: 'schema', message: (error as Error).message }], unassignedControlIds: [] } }
    }
    const template = structuredClone(input)
    if (context.additionalValidation) issues.push(...structuredClone(context.additionalValidation(structuredClone(template))))
    const profile = profiles.get(template.deviceProfileId)
    if (!profile) return { report: { valid: false, issues: [{ severity: 'error', code: 'unknown-profile', message: `Unknown device profile ${template.deviceProfileId}` }], unassignedControlIds: [] } }
    if (!validText(template.name, profile.text.mode)) issue('error', 'mode-name', `Mode name must fit device text constraints (maximum ${profile.text.mode.maximumLength}).`)
    if (template.destinationSlot !== undefined && template.destinationSlot > profile.customModeSlots) issue('error', 'slot', `Destination slot must be 1–${profile.customModeSlots}.`)
    const channelGroups = new Set([...parameters.values()].flatMap(p => p.channelGroup ? [p.channelGroup] : []))
    for (const group of Object.keys(template.channels)) {
      if (!channelGroups.has(group)) issue('error', 'unknown-channel-group', `Unknown channel group ${group}.`)
    }
    const usedChannels = new Map<number, string>()
    for (const [group, channel] of Object.entries(template.channels)) {
      const existing = usedChannels.get(channel)
      if (existing) issue('warning', 'channel-overlap', `${existing} and ${group} share channel ${channel}; receiver namespaces may collide.`)
      usedChannels.set(channel, group)
    }
    const controls = new Map(profile.controls.map(c => [c.id, c])), assigned = new Set<string>(), resolved: ResolvedAssignment[] = []
    const messageOwners = new Map<string, string>()
    for (const a of template.assignments) {
      const errorsBefore = issues.filter(i => i.severity === 'error').length
      if (assigned.has(a.controlId)) { issue('error', 'duplicate-control', 'A physical control may have only one assignment.', a.controlId); continue }
      assigned.add(a.controlId)
      const c = controls.get(a.controlId), p = parameters.get(a.parameterId)
      if (!c) issue('error', 'unknown-control', `Unknown control ${a.controlId}.`, a.controlId)
      if (!p) issue('error', 'unknown-parameter', `Unknown parameter ${a.parameterId}.`, a.controlId)
      if (!c || !p) continue
      const type = a.messageType ?? p.messageType, isButton = c.kind === 'button' || c.kind === 'pad'
      if (![p.messageType, ...p.alternateMessageTypes].includes(type)) issue('error', 'parameter-message-type', `${p.name} does not support ${type}.`, c.id)
      if ((type === 'cc' || type === 'note' || type === 'program-change') && p.number > 127) issue('error', 'message-number', 'This message type requires a number from 0–127.', c.id)
      if (!c.messageTypes.includes(type)) issue('error', 'device-message-type', `${c.name} cannot emit ${type} with this adapter.`, c.id)
      if (p.controlType === 'continuous' && isButton) issue('error', 'continuous-on-button', 'A continuous parameter requires an encoder or fader.', c.id)
      if (p.controlType === 'button' && !isButton) issue('error', 'button-on-continuous', 'A button action requires a button or pad.', c.id)
      if (p.controlType === 'selection' && type === 'cc' && isButton) issue('error', 'release-unsafe-selection', 'CC selection on a button would send a second selection on release; use a continuous control.', c.id)
      if (type === 'program-change' && !isButton) issue('error', 'program-on-continuous', 'A program selection requires a trigger button.', c.id)
      const channel = a.channelOverride ?? (p.channelGroup ? template.channels[p.channelGroup] : undefined)
      if (type !== 'realtime' && !channelValid(channel)) issue('error', 'missing-channel', `Provide channel group ${p.channelGroup ?? '(undefined)'}, or an explicit override.`, c.id)
      if (type === 'realtime' && a.channelOverride !== undefined) issue('error', 'realtime-channel', 'Real-time messages have no MIDI channel.', c.id)
      if (a.channelOverride !== undefined && p.channelGroup && a.channelOverride !== template.channels[p.channelGroup]) issue('warning', 'channel-override', `${p.name} uses channel ${a.channelOverride}; confirm the receiver is configured accordingly.`, c.id)
      const minimum = a.range?.minimum ?? p.minimum, maximum = a.range?.maximum ?? p.maximum
      if (a.range && (p.controlType !== 'continuous' || type !== 'cc')) issue('error', 'range-not-applicable', 'Ranges are supported only for continuous CC assignments.', c.id)
      if (type !== 'nrpn' && type !== 'realtime' && maximum > 127) issue('error', 'message-range', 'This message type supports values only from 0–127.', c.id)
      if (minimum < p.minimum || maximum > p.maximum) issue('error', 'parameter-range', `Range exceeds ${p.minimum}–${p.maximum}.`, c.id)
      // A mute or solo on a receiver in state mode: toggle hardware by default; the receiver's press mode (older
      // templates) keeps momentary. Effect latches stay momentary: with the receiver in state mode, on while held.
      const mute = /\.mute$/.test(p.id), solo = /\.solo$/.test(p.id)
      const latchDefault = (mute || solo) && p.buttonBehavior === 'press-or-state' && template.receiver.buttonMode === 'state' && c.buttonModes.includes('toggle') && type !== 'program-change'
      const encoderMode = a.encoderMode ?? 'absolute', buttonMode = a.buttonMode ?? (latchDefault ? 'toggle' : 'momentary')
      // Mutes and solos: bright white when off, vibrant red (mute) or green (solo) when on, where the control lights
      const colors = (mute || solo) && isButton ? { ...(c.led.off && c.led.colors.includes(3) ? { off: 3 } : {}), ...(c.led.on && c.led.colors.includes(mute ? 5 : 21) ? { on: mute ? 5 : 21 } : {}), ...a.colors } : a.colors
      if (encoderMode === 'relative' && a.range) issue('error', 'relative-range', 'Absolute value ranges are not supported on a relative encoder.', c.id)
      if (a.encoderMode !== undefined && (isButton || !c.encoderModes.includes(a.encoderMode))) issue('error', 'encoder-mode', 'Encoder mode is unsupported by this physical control.', c.id)
      if (a.buttonMode !== undefined && (!isButton || !c.buttonModes.includes(a.buttonMode) || type === 'program-change')) issue('error', 'button-mode', 'Button mode is unsupported for this assignment.', c.id)
      if (!isButton && p.controlType === 'continuous') {
        if ((encoderMode === 'relative') !== (template.receiver.encoderMode === 'relative')) issue('warning', 'encoder-receiver-mode', 'Device absolute/relative output differs from receiver interpretation; configure them separately.', c.id)
        if (encoderMode === 'relative' && profile.relativeValueFormat === 'unverified') issue('warning', 'relative-unverified', 'Actual relative-value format has not been hardware verified.', c.id)
      }
      if (p.controlType === 'button' && buttonMode === 'toggle') {
        if (p.buttonBehavior === 'held' || p.buttonBehavior === 'press' || (p.buttonBehavior === 'press-or-state' && template.receiver.buttonMode === 'press')) issue('error', 'button-receiver-mode', 'Use momentary hardware for held/press actions; mute/solo toggle hardware requires receiver state mode.', c.id)
      }
      if ((mute || solo) && buttonMode === 'momentary' && template.receiver.buttonMode === 'state') issue('warning', 'momentary-state', 'Receiver state mode will follow the held button rather than latch mute/solo.', c.id)
      if (a.colors) for (const key of ['off', 'on'] as const) {
        if (a.colors[key] !== undefined && (!c.led[key] || !c.led.colors.includes(a.colors[key]!))) issue('error', 'led-color', `${key} LED color is not supported by this profile.`, c.id)
      }
      const originalLabel = a.label ?? p.shortName, label = a.label === undefined ? defaultLabel(originalLabel, profile.text.control) : originalLabel
      if (!validText(label, profile.text.control)) issue('error', 'control-label', `Control label must fit device text constraints (maximum ${profile.text.control.maximumLength}).`, c.id)
      if (label !== originalLabel) issue('warning', 'label-adjusted', 'Default label was shortened or normalized for the controller; provide an explicit label to choose another.', c.id)
      const key = `${type}:${channel ?? 'system'}:${p.number}`, owner = messageOwners.get(key)
      if (owner) issue('warning', 'duplicate-message', `${c.id} and ${owner} emit the same MIDI address.`, c.id)
      messageOwners.set(key, c.id)
      if (issues.filter(i => i.severity === 'error').length === errorsBefore) resolved.push({ control: c, parameter: p, messageType: type, channel, number: p.number, minimum, maximum, label, encoderMode, buttonMode, ...(colors && Object.keys(colors).length ? { colors: { ...colors } } : {}) })
    }
    const unassigned = profile.controls.filter(c => !assigned.has(c.id))
    if (unassigned.length) issue('warning', 'unassigned-controls', `${unassigned.length} physical controls are unassigned and will be disabled in file export.`)
    const valid = !issues.some(i => i.severity === 'error')
    // Return a detached snapshot; callers cannot mutate the private engine context.
    const result = structuredClone({ valid, issues, profile, template, assignments: resolved, unassignedControls: unassigned })
    return { report: { valid, issues: result.issues, unassignedControlIds: unassigned.map(c => c.id) }, result }
  }
  function resolve(template: ControllerTemplate): MappingResult {
    const { report, result } = analyze(template)
    if (!report.valid || !result) throw new MappingValidationError(report.issues)
    return result
  }
  return {
    validate(input: unknown): ValidationReport { return analyze(input).report },
    resolve,
    exportFile(template: ControllerTemplate, serializer: FileSerializer): Uint8Array {
      const mapping = resolve(template)
      if (serializer.deviceProfileId !== mapping.profile.id) throw new MappingValidationError([{ severity: 'error', code: 'serializer-profile', message: 'Serializer does not match the template device.' }])
      return serializer.serialize(mapping)
    },
  }
}
