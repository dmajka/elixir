/** Portable semantic contracts. No UI, transport, or device codec dependencies. */
export type MidiMessageType = 'cc' | 'note' | 'program-change' | 'realtime' | 'nrpn'
export interface MappingParameter {
  readonly id: string
  readonly name: string
  readonly shortName: string
  readonly channelGroup?: string
  readonly messageType: MidiMessageType
  readonly alternateMessageTypes: readonly MidiMessageType[]
  readonly number: number
  readonly minimum: number
  readonly maximum: number
  readonly controlType: 'continuous' | 'button' | 'selection' | 'realtime'
  readonly buttonBehavior?: 'press-or-state' | 'held' | 'press' | 'tap-latch-hold'
}
export interface TextConstraint { maximumLength: number; ascii?: boolean }
export interface PhysicalControl {
  readonly id: string
  readonly name: string
  readonly kind: 'encoder' | 'fader' | 'button' | 'pad'
  readonly group: string
  readonly row: number
  readonly column: number
  readonly messageTypes: readonly MidiMessageType[]
  readonly encoderModes: readonly ('absolute' | 'relative')[]
  readonly buttonModes: readonly ('momentary' | 'toggle')[]
  readonly led: { readonly off: boolean; readonly on: boolean; readonly colors: readonly number[] }
}
export interface DeviceProfile {
  readonly id: string
  readonly manufacturer: string
  readonly model: string
  readonly generation: string
  readonly identity: { readonly productBytes: readonly number[]; readonly portNameHints: readonly string[] }
  readonly controls: readonly PhysicalControl[]
  readonly customModeSlots: number
  readonly text: { readonly mode: TextConstraint; readonly control: TextConstraint }
  readonly protocol: { readonly format: string; readonly fileExport: 'verified' | 'unverified'; readonly hardwareUpload: 'unverified' | 'verified' }
  readonly firmware: { readonly minimumBuild?: number; readonly relativeEncoderBuild?: number; readonly hardwareTested: boolean }
  readonly feedback: 'unverified' | 'verified'
  readonly relativeValueFormat: 'unverified' | '64-offset' | '1-127'
  readonly limitations: readonly string[]
}
export interface MidiAssignment {
  controlId: string
  parameterId: string
  messageType?: MidiMessageType
  channelOverride?: number
  range?: { minimum: number; maximum: number }
  label?: string
  encoderMode?: 'absolute' | 'relative'
  buttonMode?: 'momentary' | 'toggle'
  colors?: { off?: number; on?: number }
}
export interface ControllerTemplate {
  schemaVersion: 1
  id: string
  name: string
  deviceProfileId: string
  channels: Record<string, number>
  receiver: { encoderMode: 'pickup' | 'absolute' | 'relative'; buttonMode: 'press' | 'state' }
  /** Human-readable slot, 1-based. File serialization never writes to this slot. */
  destinationSlot?: number
  assignments: MidiAssignment[]
}
export interface MappingIssue { severity: 'error' | 'warning'; code: string; message: string; controlId?: string }
export interface ResolvedAssignment {
  control: PhysicalControl
  parameter: MappingParameter
  messageType: MidiMessageType
  channel?: number
  number: number
  minimum: number
  maximum: number
  label: string
  encoderMode: 'absolute' | 'relative'
  buttonMode: 'momentary' | 'toggle'
  colors?: { off?: number; on?: number }
}
export interface MappingResult {
  valid: boolean
  issues: MappingIssue[]
  profile: DeviceProfile
  template: ControllerTemplate
  assignments: ResolvedAssignment[]
  unassignedControls: PhysicalControl[]
}
export interface FileSerializer {
  readonly deviceProfileId: string
  readonly extension: string
  serialize(mapping: MappingResult): Uint8Array
}
