import { decodeFile } from './codec'
/** Hardware acceptance applies only to slot1/build1563/absolute CC and the observed success replies. */
export const XL3_TRANSFER_EVIDENCE = Object.freeze({
  identity: true, firmware: true, slotAddressing: true, acknowledgement: true, readBack: true, browserUpload: true,
})
export const XL3_TRANSFER_BLOCKERS = Object.freeze([
  'Other custom-mode slots require separate hardware validation.',
  'Other firmware builds require identity and protocol validation.',
  'Unknown/error reply schemas remain unsupported and time out safely.',
  'Reconnect persistence after the standalone write and unrelated-slot preservation are not verified.',
])
/** Offline file inspection cannot authorize an upload; live identity/backup/confirmation is still required. */
export function inspectXL3TransferFile(bytes: Uint8Array, slot: number) {
  if (!Number.isInteger(slot) || slot < 1 || slot > 15) throw new Error('Choose a custom-mode slot from 1 to 15.')
  const decoded = decodeFile(bytes)
  if (decoded.destination !== 127) throw new Error('Expected an XL3 export file with destination 127.')
  return { slot, name: decoded.mode.name, byteLength: bytes.length, surfaces: 2,
    uploadEnabled: false as const, backupAvailable: false as const,
    blockers: [...XL3_TRANSFER_BLOCKERS],
    verification: { browserAccepted: 'not-attempted', deviceAcknowledged: 'not-attempted', stored: 'not-verified', physicalOutput: 'not-verified' } as const }
}
