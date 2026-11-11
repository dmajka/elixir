# XL3 transfer protocol and validation status

**Current status:** direct browser upload, matching replies, read-back and representative physical output verified on build1563/slot1/absolute CC. Other live-transfer combinations are blocked. See the current acceptance section below and `hardware/README.md` for captures. Earlier sections retain the historical research gates.

## Historical Phase2 status

The file codec is intentionally separate from Web MIDI. `encodeFile()` always emits export destination 127; there is no public hardware-write helper, no upload UI, no ACK matcher and no MIDI send call in the codec. Do not repurpose its output as an upload until the hardware phase supplies evidence.

## Source-derived candidate flow

Phase 1 evidence E08 in [evidence-log.md](evidence-log.md) identifies:

1. Confirm identity/firmware and main input/output, separately from DAW/DIN ports.
2. Select one of 15 custom slots. Serializer receives the selected zero-based index; DAW activation uses a separate translation (`i + 6` for 0–3, `i + 14` for 4–14).
3. Serialize encoder surface 0 and fader/button surface 3, with the target index instead of file sentinel 127.
4. Register a reply listener before sending each frame, then await a response. The source's loose predicate checks only byte 8 == 0x15; this is insufficient validation for our transport.
5. Source request timeout is 1,000 ms; cleanup removes the listener. No complete ACK status/error schema, retry behavior or persistence proof has been observed.

Candidate read-back request: `F0 00 20 29 02 15 05 00 40 surface slot F7`. The source accepts several response operations. Read-back could enable backup and comparison, but even this request was not sent in Phase 2. No device was connected in Components during the file investigation.

## What was actually verified

- Independently generated file bytes match Components export format.
- Components imports our mixed mode and re-exports identical bytes.
- Generated output is seven-bit-clean inside framing, with strict validation of the implemented subset.

These findings establish file compatibility. They do not establish browser send acceptance, device acknowledgement, durable storage, actual physical output, LED behavior or preservation of other slots.

## Hardware-phase gate

Before any write: confirm exact model and firmware; capture known-good read/ACK/error responses; validate and correlate manufacturer, product, operation, surface and destination; choose a slot; explain overwrite; offer a backup where read-back has been verified; obtain explicit confirmation. Send only validated messages. Then verify read-back, persistence and representative physical controls, recording each verification level separately. Handle disconnects/timeouts and partial success after the first surface without blindly retrying.

Until then, retain Components file export/import as the verified path. Launchpad Pro has its own protocol and remains outside this codec.

## Phase 7 implementation — partial, awaiting hardware evidence

The standalone UI now supports requesting Web MIDI SysEx permission, explicit input/output enumeration, input selection and bounded passive message capture. `src/midi/web-midi.ts` exposes no send method. It handles permission rejection, missing SysEx permission, a10-second request timeout, stale/cancelled permission results, hotplug, input removal and listener/port cleanup. Permission is requested only by the Connect button. Input capture does not probe identity or send any custom commands.

`HardwareConnection.vue` shows slot1–15 selection, validates the generated export through the independent decoder, explains the future overwrite of both surfaces, and shows the four separate verification levels. Upload stays disabled unconditionally. No checkbox or user-supplied JSON can mark the shipped protocol evidence as verified. Identity/firmware detection, upload frames, ACK matching, hardware backup and durable-storage comparison remain unimplemented pending real captures. Progress for an actual transfer cannot be reported without a verified transfer protocol.

`transfer.ts` performs offline inspection only; the generated file retains destination127. Choosing a destination in the connection panel does not rewrite the export or transmit anything. No backup claim is made: users should preserve existing configurations using Components before any future hardware write. Existing `.syx` export remains available independently of permission or hardware.

### Gathering the missing evidence

- Confirm an actual Launch Control XL3 is available, record firmware/version and exact main/DAW/DIN port names, and preserve the existing chosen slot using Components.
- Capture a known-good Components transaction with an appropriate OS MIDI capture/routing setup. Record both outgoing and incoming bytes with direction, timestamps, exact model/firmware, chosen human slot, both surfaces and outcome. The browser's passive input capture sees only messages delivered to that selected input; it cannot intercept another application's sends and may contend with Components for access.
- Establish identity/firmware response layout and distinguish success/error status. Verify which fields correlate ACKs to operation, surface and destination; do not use the source's byte8-only predicate.
- Verify read-back for both surfaces, backup/restoration, persistence after reconnect, unrelated-slot preservation and representative physical controls. Keep browser send acceptance, ACK, storage and physical output results separate.
- Only after these checks establish a supported protocol, implement a strictly validated sender with listener-before-send, timeout/cancellation, disconnect handling, partial-success reporting, no blind retry, and explicit confirmation bound to the reviewed device, mapping and slot.

No real device access, identity request, read-back request or hardware write occurred during implementation.224 automated tests pass across eight files, including mocked Web MIDI permission/capture/disconnect and closed upload gate checks; TypeScript and production build pass. These are software checks, not Phase7 hardware acceptance. Browser visual QA remains pending under the existing localhost restriction.

API behavior was checked against the [W3C Web MIDI specification](https://www.w3.org/TR/webmidi/) (permission, maps, event delivery and implicit input opening). Captures use DOM event timestamps in milliseconds relative to the browsing session, rather than claiming wall-clock time.

## Standalone protocol implementation after hardware captures

Added `hardware-protocol.ts` to strictly parse the captured universal identity, operation0x15 replies and operation0x10 hardware read-back. Version decoding cross-checked against Components' `versionFromBytes`: bytes14/15 yield12×128+27=1563. The supported identity is family200 (wire48 01), member0, application version bytes01 02 0c 1b. This is distinct from proprietary protocol product02 15.

Hardware read-back CC containers omit a trailing zero minimum. The parser reconstructs only observed8/9-byte CC payloads, validates explicit channels and known metadata, and passes reconstructed data through the independent canonical decoder. Unknown types, metadata/settings, addresses, missing/duplicate surfaces and malformed containers fail closed. Comparison allows precisely the observed continuous-flag normalization and otherwise requires byte-identical canonical encoding. Backup download uses the validated reconstructed file, with raw replies also retained in the preparation API.

Added the scoped Web MIDI request transport and `createXL3Uploader`:

- Input/output pair scoped to one connection; register listener before send; one outstanding request; strict reply matching; timeout/abort/disconnect cleanup; no retries.
- `prepare()` sends identity enquiry then reads both surfaces. Limited to human slot1, exact captured firmware/build and absolute explicit-channel CC/off files. It snapshots the reviewed file and backup, and produces an opaque one-use confirmation token.
- Upload execution logic rechecks identity/original slot, rejects changed file/slot/connection/token, sends surfaces sequentially, tracks browser acceptance independently from matching replies, compares read-back and reports partial success. It does not activate modes through DAW traffic.
- Candidate write frames are prepared offline from validated export bytes with source-derived destination0. Reply address6 is separately observed; it is never substituted for the write index.
- **Release write gate remains closed** in `hardware-policy.ts`. It is not configurable through the UI or caller input. Simulator tests replace the gate only inside Vitest; no hardware was accessed. Unknown error replies time out rather than being accepted as success. Full error/status semantics and independent outgoing/browser write verification remain open.

`HardwareConnection.vue` now offers Verify device & back up slot1, a backup download, cancellation and explicit progress. Port/mapping/slot changes cancel/invalidate preparation. The overwrite control remains disabled by the release gate. No further hardware write occurred in this implementation step.

Validation:243 tests across12 files pass; TypeScript and production build pass. New tests exercise real captured identity/read-back fixtures, response framing/surface/address rejection, successful simulated upload, one-use/changed confirmation rejection, partial-send timeout, permission/port failure, request cancellation, synchronous replies and read-only UI backup. Actual browser permission/input delivery and outgoing requests still require verification in an unrestricted browser/terminal; the agent's localhost/MIDI sandbox limitations are unchanged.


## Current Phase7 acceptance — direct browser path passed for the supported subset

The user explicitly approved one independent-browser write to slot1. The actual sender wrote two frames with operation0x45, surfaces0/3 and zero-based index0; both12-byte operation0x15 replies matched the complete manufacturer/product/command/version/surface/address6 framing. A fresh operation0x10 read-back compared successfully, allowing only the documented continuous-flag normalization. The complete20-entry bidirectional trace is saved under `hardware/slot1-direct-upload-transaction.json`; canonical returned file under `hardware/slot1-direct-upload-readback.syx`.

After this direct write, the user exercised controls and the browser recorded194 CC messages: encoder1 CC72 (25), encoder row2/column1 CC56 (21), encoder row3/column1 CC64 (17), fader1 CC8 (127), mute1 CC104 (press127/release0), solo1 CC112 (press127/release0). All channel15, matching Mixer. Saved `hardware/slot1-direct-upload-physical.json`. This short movement check does not claim full range coverage for this new sample; the earlier Components-written mode did reach0–127.

The policy now permits the validated slot1/build1563/absolute-CC subset with live identity/backup/recheck, backup download and explicit bound overwrite confirmation. Other slots/builds/message types remain blocked. Reconnect persistence passed earlier through Components but was not repeated after the independent write; cross-slot preservation and restoring a backup remain untested. Unknown error replies are rejected/timed out rather than guessed. No retry, second write or additional user action is required for this completed representative-output check.

247 tests across12 files and the production build pass. These include real immutable transaction/physical fixtures plus simulations for timeout, disconnect, cancellation and partial success. `XL3_PROFILE` marks hardware upload/test evidence verified with a prominent scope limitation; this must never be read as validation of all15 slots or all file-codec capabilities.

## Phase8 acceptance update

Explicitly approved slot1 changed-channel test and Mixer restoration both passed actual browser sends, both matching replies and stored comparison on build1563. Physical capture verifies global channel7 and track channel3 across all six groups; restoration used original15/16 and canonical read-back equals pre-test Mixer bytes. Components read/export snapshots for all15 slots before/after prove no net change in slots2–15 across this two-write sequence. Hashes/traces and operational details in `hardware/README.md`; monitor acceptance in `midi-monitor.md`.259 tests/build pass. This closes Phase8 for the supported subset. No broader live-slot/message/firmware support or general backup-import UI is implied. Reconnect persistence after these particular writes and relative/LED behavior remain unverified.
