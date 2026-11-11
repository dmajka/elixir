# Phase 9 — Launchpad Pro investigation

Started 2026-11-10 after reading the PRD and progress log. User confirmed Launchpad Pro MK3 and connected it on 2026-11-10. Components independently displays MK3 connected/ready. Phase9 is complete for the supported single-pad subset. Direct browser upload is verified on slot1/build483 with explicit-channel momentary CC/off pads, including physical Mute/Solo output and exact original restoration. Broader transfer compatibility remains documented below.

## Generation gate

PRD sections 4.1 and Phase 9 require confirming the exact generation before implementing its protocol. Existing evidence E10 identifies **Launchpad Pro MK3**, not the user's device. Do not infer the generation from the project name, apply the XL3 adapter to Launchpad, or enable programming based on a port-name match.

Novation's [Components Custom Mode Editor Guide](https://support.novationmusic.com/hc/en-gb/articles/360009860380-Launchpad-Components-Custom-Mode-Editor-Guide) explicitly covers Pro MK3, X and Mini MK3. It documents offline editing, per-widget channels, CC buttons, note pads, program changes and CC fader widgets. Note pads support momentary/toggle; CC buttons additionally support trigger. These are documented editor capabilities, not independently verified serializer or physical-output behavior.

Novation's [Pro MK3 Setup guide](https://userguides.novationmusic.com/hc/en-gb/articles/25494545308306-Using-the-Launchpad-Pro-MK3-s-Setup-page) distinguishes Live/Custom, Programmer and Legacy modes. Programmer mode uses fixed message addresses and disables the other modes; Legacy recreates the original Pro User layout. Neither should be conflated with stored Custom Mode programming. The guide describes MIDI lighting and refers to the Programmer Reference for details; Elixir feedback compatibility remains untested.

## Rechecked local source evidence

Source: the hashed Components 1.72.5 snapshot recorded in `evidence-log.md`, E10. Inspection is read-only; proprietary code remains under ignored `research/extracted/`.

| Observation | Locator in `research/extracted/main.pretty.js` | Limit |
| --- | --- | --- |
| Explicit MK3 service; product `02 0E` | 69055–69165 | Not an identity for the user's device |
| Custom-mode wrapper `F0 00 20 29 02 0E 05 … F7` | Same service, `customModeSysex` | Payload differs from XL3; no send authorized |
| Minimum build 430; binary version 1 at build >=430, otherwise 0 | `minimumFirmwareVersion`, `customModeVersion` | Snapshot behavior, not cross-firmware acceptance |
| Writer receives name truncated to 32 characters, version, mode index | `sendPack` | Byte encoding/limits still need fixture checks |
| One binary send followed by 100 ms delay | `sendPack` | Delay is not an ACK or proof of storage |
| Read path wraps an index and waits for a mode response | `getFromDevice` | No actual response/correlation evidence yet |
| Eight destination choices | 112160–112200 | Index translation must be checked independently |

## Implementation and acceptance sequence

1. Confirm original Pro versus MK3, physical availability and firmware. If original Pro, investigate its own documented capabilities before choosing an approach; do not promise MK3 custom-mode storage.
2. For the confirmed generation, collect genuine offline Components baseline and isolated note, CC, channel, behavior, color, name and program-change fixtures where supported. Record provenance and hashes. Inspect binary writer/parser without importing proprietary code into application source.
3. Implement a strict independent file codec and profile for the proven subset. Check generated exports by Components import/re-export separately from hardware transfer.
4. Use the shared engine for pad assignments and a grid preview. Start with documented Elixir mute/solo and pattern selection. Performance and macro controls require an explicit widget/range model. Track triggering requires documented target note/channel semantics; do not invent remote trigger messages.
5. Route device-specific templates and file adapters by profile. Audit XL3 assumptions in mapping context, preset selection, preview, downloads, monitor and HardwareConnection before exposing Launchpad in the UI. Never let a Launchpad mapping enter the XL3 transaction path.
6. Investigate LED feedback independently of custom-mode configuration. Record local lighting versus external feedback, address/channel/color conventions and routing requirements.
7. Before any write, obtain known-good identity/read-back evidence and a backup. Prepare a concrete mapping and destination for explicit overwrite confirmation. Validate browser sends, replies/storage, physical output, restoration and unrelated-slot preservation separately. No speculative SysEx probes.

Phase 9 implementation and acceptance results are recorded below.

## Phase 9 implementation and file acceptance

- `src/devices/novation/launchpad-pro-mk3/`: independent version-1 file codec, 64-pad profile and semantic adapter. Eight custom slots are recorded, but serializer always uses file sentinel127 and exposes no hardware-send API.
- Genuine blank, CC, changed-channel, toggle, note and program fixtures establish the single-pad record fields. Independent mixed file imported successfully and its re-export matches all fields/bytes apart from the filename-derived name. Fixtures, provenance and hashes: `src/tests/fixtures/launchpad-pro-mk3/`.
- `src/elixir/launchpad-templates.ts`: Mute/Solo (16 pads), Patterns (32 pads), Performance (26 pads). Registry has32 pattern choices per remote channel group, not64 patterns. No new Elixir message definitions were invented. Continuous macros and voice-note triggering remain deferred.
- Shared UI now switches profiles, presets, editor compatibility, grid size, blank/imported templates and file serializers. Pads use button compatibility in parameter browsing. The MK3 connection retains passive monitor/simulation and hides the XL3 transfer workflow; serializer mismatch also rejects cross-device export.
- Per-pad labels remain workspace metadata; binary widgets have no labels. Off colors are editable; active color is shared green21. This is configuration support, not verified external Elixir feedback.
- `tools/launchpad/prepare.ts` regenerates a concrete Mute/Solo candidate in `research/hardware/launchpad-pro-mk3/` and the mixed acceptance fixture without MIDI access.

### Initial hardware backup

Through Components Get Custom Mode →1, retrieved `Drums Square` and saved `research/hardware/launchpad-pro-mk3/slot1-Drums-Square-backup.syx` (540bytes). Independent decoding/re-encoding preserves the backup exactly. This confirms read-only Components retrieval/export, not a standalone browser read path. Firmware build has not yet been independently identified.
 
### Components transfer and restoration

Components wrote Mute/Solo to slot 1 and read-back matched the prepared settings apart from the filename-derived name. Physical capture returned channel 1 note and pressure events and zero expected channel 15 CCs. The active hardware mode during capture could not be verified. Components restored the original Drums Square mode. The fresh slot 1 export matches the original backup byte for byte. See [hardware evidence](hardware/launchpad-pro-mk3/README.md).

Phase 9 file interoperability is verified for the single-pad subset. Physical Mute/Solo acceptance and standalone browser transfer remain open.


### Browser read-back preparation

Rechecked the slot selector implementation in the hashed Components snapshot. The MK3 editor passes offset4 to its eight-choice selector. The shared selector adds that offset to the zero-based button index and passes the result to the service. Human custom mode1 therefore uses protocol index4. Source locators are main.pretty.js86620–86695,112160–112210 and the MK3 service69055–69165. The initial investigation had left index translation open.

Added a scoped browser identity/slot1 read workflow in `hardware-protocol.ts` and `LaunchpadTransfer.vue`. It uses universal device inquiry, validates MK3 application family/member fields and decimal firmware digits, then requests index4 with the source-derived read command. It normalizes the reply destination to127 for file decoding/export. Logging retains SysEx traffic and the selected port snapshot. Tests cover wrong identity, decimal firmware, slot correlation and exact read-only requests. Actual browser acceptance awaits the user-started Vite server.

Novation's [custom-mode selection guide](https://userguides.novationmusic.com/hc/en-gb/articles/25494530115346-Launchpad-Pro-MK3-interface) identifies Track Select on the second-from-bottom row as the eight custom-mode selectors. Repeat physical testing should confirm the leftmost Track Select button lights pale green after pressing Custom.

### Actual browser read acceptance and write preparation

Chrome verified the connected MK3 application identity and build483. Slot1 request index4 returned Drums Square, and canonical backup matches Components byte-for-byte. Four-entry trace and backup are preserved in the hardware evidence directory. Fixed same-endpoint enumeration cancelling an active read, with regression coverage.

The uploader is implemented for a slot1/build483 momentary explicit-channel CC test. It binds the candidate and original backup, rechecks before sending and compares stored contents afterward. The original backup can then be restored and compared through the same session. Production write gate is closed while awaiting approval for the prepared direct-browser test. All282 tests and build pass.


### Phase 9 supported-subset acceptance complete

Quick-tap/release mode selection latched Custom mode1. The restored original first emitted note0/channel5 as its saved top-left assignment specifies. Repeated the approved temporary Mute/Solo upload with a fresh byte-identical original backup. Stored comparison passed. Actual physical capture contains exactly32 CC messages: each CC104–119 on channel15 emitted127 press and0 release. Regression replay checks every pad against the shared semantic engine.

The browser then restored Drums Square. A fresh read-back equals the original540-byte backup exactly. Both13-entry upload and22-entry combined restoration logs are untruncated. MIDI was disconnected after evidence downloads. Current UI describes the verified limits and quick-tap selection. All285 tests across16 files and TypeScript/production build pass. Final desktop transfer panel was visually inspected.

Supported delivery includes64-pad profile/grid/editor, Mute/Solo,32-pattern and Performance presets, CC/note/program file exports, Components interoperability, passive monitoring/simulation, identity/backup and guarded direct upload for slot1/build483/momentary explicit-CC mappings. Note/program/toggle live output, other write slots/builds, global/changed-channel physical testing, reconnect persistence, unrelated-slot preservation, narrow MK3-specific visual QA and external LED feedback remain unverified. Fader widgets/continuous macro presets, surrounding navigation assignment and voice triggering remain deferred. Phase10 integration packaging awaits a separate continuation.
