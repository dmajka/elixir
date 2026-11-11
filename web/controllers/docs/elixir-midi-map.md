# Elixir remote MIDI registry — Phase 3

## Source and coverage

Authoritative source: the user-supplied `reference/elixir_cheatsheet.pdf`, pages 5 and 6. Text was extracted locally with Poppler; the full page-5 image and page-6 image were visually reviewed. Phase 1/2 Novation research is not used to infer Elixir parameter numbers. No Elixir firmware definitions were supplied in this standalone reference directory, so firmware cross-checking remains pending.

`src/elixir/parameter-registry.ts` exports 304 stable semantic entries:

| Group | Count | Coverage |
| --- | ---: | --- |
| Remote Tracks page 6 | 112 | 14 rows × 8 columns: Volume, Pan, Filter frequency, Resonance, Decay, Pitch, Delay send, FX send, Reverb send, Sample rate, LFO amount, Density, Mute, Solo |
| Remote Global continuous | 62 | CC8–47 grid, delay CC96–103, Ring CC3/4/5/7/63, LFO4 CC2, roll faders CC112–119 |
| Remote Global buttons | 55 | CC/note48–62, 64–95, 104–111 |
| Shared remote-channel program selections | 64 | 32 pattern choices on each of Remote Tracks and Remote Global |
| Shared remote-channel CCs | 8 | Master volume124, morph kit125, slot kit126, factory kit127 on both groups |
| System real-time | 3 | MIDI Clock F8, Start FA, Stop FC; no channel |

There are 71 button entries with alternate note messages: 16 track mute/solo controls and 55 global buttons. Alternatives use the same semantic ID and number rather than duplicate parameter records. Every generated event identifies its message type explicitly. A pattern is a two-byte Program Change; it cannot be substituted with a CC.

Track columns remain D1, D2, D3, SN, CY, CH, OH, selected. CH/OH remote columns stay separate, even where the sound engine shares the hat voice. The selected column follows the selected track. Conventional voice controls (e.g. D1 Volume CC89), normal-global CC102/103 kit aliases, and normal-track CC0 are intentionally outside this remote registry; remote D1 Volume remains CC8.

## API

All modules are framework-independent and perform no MIDI I/O.

```ts
import { DEFAULT_CHANNEL_SETTINGS, channelWarnings } from './channel-settings'
import { getParameter, resolveParameter, filterParameters } from './parameter-registry'

const channels = { ...DEFAULT_CHANNEL_SETTINGS } // remoteTracks:15, remoteGlobal:16
getParameter('tracks.d1.volume')
resolveParameter('tracks.d1.volume', channels, { value: 100 }).bytes // BE 08 64
resolveParameter('tracks.d1.mute', channels, { messageType: 'note' }).bytes // 9E 68 7F
resolveParameter('tracks.d1.mute', channels, { messageType: 'note', release: true }).bytes // 8E 68 00
resolveParameter('global.pattern.32', channels).bytes // CF 1F
filterParameters({ query: 'reverb', track: 'ch', category: 'Sends' })
channelWarnings(channels)
```

- `getParameter(id)` throws for unknown IDs.
- `PARAMETER_REGISTRY` and nested metadata are frozen. Explicit host overrides will be handled at the mapping/integration boundary rather than mutating this source.
- Registry fields include stable ID, name/short name, category, scope, track where applicable, symbolic channel group, primary/alternate MIDI types, number, min/max, interpretation, button/encoder guidance, optional USB feedback evidence and page/section reference.
- `resolveParameter()` validates both remote channels, optional type substitution, event kind and value. It returns the parameter, type, resolved human-readable channel (undefined for real-time), number and bytes. Public channels remain 1–16; only encoding subtracts one. CC/note value defaults to the parameter maximum; button release is always zero. Continuous callers should supply the observed/desired value explicitly. Program and real-time events reject value overrides.
- `findParametersForMessage(type, number, channel, settings)` returns ALL matching entries. If remote channels overlap it returns both namespaces, allowing the monitor/editor to expose ambiguity instead of silently choosing one. Pass undefined channel for real-time events.
- `filterParameters()` combines case-insensitive ID/name/category text search with exact category, track, scope and primary-or-alternate type filters.
- `DEFAULT_CHANNEL_SETTINGS` is immutable. `channelWarnings()` checks remote overlap and conventional track/global overlap (defaults tracks1–7/global10); callers can supply their configured conventional channels. It never changes channels or rejects an overlap. Invalid channels (noninteger or outside1–16) throw.

The visible Phase 0 interface remains a simulation; this phase supplies the registry API for the later editor rather than adding premature mapping/upload UI.

## Semantics and limits

- Buttons: CC>0/note-on presses; CC0/note-off releases. Mute/solo PRESS flips state on press; TOGGLE follows button state. The registry records receiving semantics. It does not implement a toggle state machine, guess hardware toggle capability or schedule button timing.
- Effects and Control All: tap under ¼s latches, again off; longer press holds; toggle pads follow state. Track selection64–70 instead toggles Control All inclusion when that function is active; CC71 reverts its changes. Context is in interpretation metadata.
- Track density is centered at64. Roll faders: 0 off, 1–32 grid (1/4…1/32), 33–127 free (1/32…1/256). Their placement under a button heading is not evidence that note messages encode a continuous rate, so only CC is exposed.
- Delay Time/Wow/Glitch become Grain Position/Size/Density; Saturate becomes Grain Scatter. Ring root/decay reuse master filter12 and filter resonance47. Aliases are recorded in names/interpretation rather than duplicate conflicting controls.
- Physical scaling for tempo, filters, etc. is not provided by the sheet. Values are retained as 0–127 without invented BPM/Hz conversion.
- CC63 explicitly controls Ring spread. The global button table's adjacent “tap/hold knob” text does not define another action. `UNRESOLVED_REMOTE_ENTRIES` preserves the annotation and firmware-verification requirement. No note63 button is fabricated.
- Exact record/play press behavior is not fully specified beyond the transport/held grouping; metadata calls this out for firmware/hardware verification.
- Elixir PICKUP/ABS/REL is receiving behavior. REL accepts the documented 64±n or1/127 conventions; actual XL3 relative output compatibility is still unverified. The registry does not equate Elixir REL with a Novation flag.
- USB feedback is documented by Elixir; controller compatibility remains explicitly unverified. This phase does not establish LED feedback routing.
- MIDI real-time statuses are standard MIDI encodings for the messages named on page5. Including them does not claim XL3 custom controls can emit them. The mapping engine must report that capability gap. Clock output is an event definition, not a scheduler.
- NRPN and conventional voice maps remain future separate namespaces; no undocumented firmware NRPN numbers or realtime Continue message were invented.

## Verification

`src/tests/elixir-registry.test.ts` independently transcribes the page-6 chart and checks all 112 track cells, every global knob/button, alternate note presses/releases, roll-rate boundaries, all 64 pattern choices and eight shared CC entries. It also checks channel1–16 encoding, registry lookup/filtering, metadata, overlap warnings without mutation, collision preservation, unknown IDs and invalid values/types. All registry entries resolve and reverse-match.

Phase 3 validation: 168 tests across four files passed; TypeScript checks and the production build passed. This verifies registry/data encoding in software, not firmware interpretation, physical controls, upload or LEDs. The earlier file-codec acceptance evidence remains unchanged.
