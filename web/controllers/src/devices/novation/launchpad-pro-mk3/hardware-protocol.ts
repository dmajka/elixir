import { decodeLaunchpadFile, type LaunchpadMode } from './codec'
import type { MidiRequestTransport } from '../../../midi/request-transport'

// Components 1.72.5 supplies offset4 to both read and send slot selectors.
export const MK3_SLOT1_INDEX = 4
/** Custom mode 1-8 as the device's index (Components: offset 4 plus the zero-based choice). Every write reads the
 * slot first, so a wrong index fails before anything is sent. */
export function mk3SlotIndex(slot: number) {
  if (!Number.isInteger(slot) || slot < 1 || slot > 8) throw new Error('Choose a custom mode from 1 to 8.')
  return MK3_SLOT1_INDEX + slot - 1
}
export const mk3ReadRequest = (slot: number) => Uint8Array.of(0xf0, 0, 0x20, 0x29, 2, 14, 5, mk3SlotIndex(slot), 0xf7)
export interface LaunchpadIdentity { deviceId: number; firmwareBuild: number; versionBytes: number[] }
export const MK3_IDENTITY_REQUEST = Uint8Array.of(0xf0, 0x7e, 0x7f, 6, 1, 0xf7)
export const MK3_SLOT1_READ_REQUEST = Uint8Array.of(0xf0, 0, 0x20, 0x29, 2, 14, 5, MK3_SLOT1_INDEX, 0xf7)
const modePrefix = [0xf0, 0, 0x20, 0x29, 2, 14, 5, 1]

export function parseLaunchpadIdentity(bytes: Uint8Array): LaunchpadIdentity {
  const prefix = [0xf0, 0x7e]
  if (bytes.length !== 17 || !prefix.every((b, i) => bytes[i] === b) || bytes[3] !== 6 || bytes[4] !== 2 ||
    ![0, 0x20, 0x29, 0x23, 1, 0, 0].every((b, i) => bytes[i + 5] === b) || bytes[16] !== 0xf7 ||
    bytes.slice(1, -1).some(b => b > 127) || bytes.slice(12, 16).some(b => b > 9)) {
    throw new Error('Expected Launchpad Pro MK3 application identity with decimal firmware digits')
  }
  const versionBytes = [...bytes.slice(12, 16)]
  const firmwareBuild = versionBytes.reduce((build, digit) => build * 10 + digit, 0)
  if (firmwareBuild < 430) throw new Error('MK3 binary version1 requires firmware build430 or later')
  return { deviceId: bytes[2]!, firmwareBuild, versionBytes }
}

export function isLaunchpadSlot1Reply(bytes: Uint8Array, slot = 1): boolean {
  return modePrefix.every((b, i) => bytes[i] === b) && bytes[8] === mk3SlotIndex(slot) && bytes.length > 32
}
export function decodeLaunchpadSlot1(bytes: Uint8Array, slot = 1): { mode: LaunchpadMode; backup: Uint8Array } {
  if (!isLaunchpadSlot1Reply(bytes, slot)) throw new Error('Expected MK3 version1 slot1 response (index4)')
  const backup = bytes.slice()
  backup[8] = 127
  const mode = decodeLaunchpadFile(backup)
  return { mode, backup }
}
export function isLaunchpadMainPort(name: string): boolean {
  return /LPProMK3|Launchpad Pro MK3/i.test(name) && !/DAW|DIN|Bootloader|MIDI[ ]?[23]|MIDIIN|MIDIOUT/i.test(name)
}

export async function readLaunchpadSlot1(transport: MidiRequestTransport, signal?: AbortSignal, slot = 1) {
  const identityBytes = await transport.exchange(MK3_IDENTITY_REQUEST, bytes => bytes.length === 17 && bytes[1] === 0x7e && bytes[3] === 6 && bytes[4] === 2, signal)
  const identity = parseLaunchpadIdentity(identityBytes)
  const reply = await transport.exchange(slot === 1 ? MK3_SLOT1_READ_REQUEST : mk3ReadRequest(slot), b => isLaunchpadSlot1Reply(b, slot), signal)
  return { identity, ...decodeLaunchpadSlot1(reply, slot) }
}
