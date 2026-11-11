# Launch Control XL 3 custom-mode format — Phase 2

## Validation status

**File interoperability verified against Components 1.72.5 on 2026-11-10. Hardware upload is not verified.**

The installed ARM64 archive has SHA-256 `45d5840d52ec0c401e81194ba445efa3ef3e7c5b950aed53e3483176a3fe410e`, matching the Phase 1 supplied snapshot. The app showed no connected XL3 throughout offline testing. No sign-in, cloud save, firmware update or send-to-device operation was used.

The independent encoder produces the genuine 684-byte default export exactly. Components imported an independently generated 678-byte mixed-control mode, displayed its assignments correctly, and re-exported **identical bytes**. Test fixtures and provenance are in `src/tests/fixtures/xl3/manifest.json` and its README. These are our new configuration exports, not copied application code/assets.

## Confirmed format

The framing and container layout documented in [novation-protocol.md](novation-protocol.md) were confirmed by exported files:

- Two messages, header `F0 00 20 29 02 15 05 00 45 surface 7F`, terminator `F7`.
- Surface 0: encoders 16–39. Surface 3: faders 40–47 and buttons 48–63. IDs are decimal.
- Mode-name container `20 length text`. Control specifications use `40 | length`, ID, payload. Labels use `60 | length`, ID, text.
- Fader specifications/labels precede button specifications/labels on surface 3.
- Complete default mode: 684 bytes, two 342-byte frames. No checksum bytes found in this file path.
- Explicit MIDI channels are zero-based on wire. Components' Global channel uses flag 0x40 plus channel nibble 1; it is NOT an explicit channel 2 assignment.
- Note control has eight payload bytes; CC and Program Change have nine. Disabled control has zero payload bytes plus its label container.

For the default fixture, encoder 1 starts at file offset 29. The two-byte record prefix is followed by type, off/on colors, channel/behavior, flags, sensitivity, CC number, top and bottom value. Offsets below are zero-based; values are hexadecimal.

| Isolated UI change | File evidence |
| --- | --- |
| Encoder 1 CC 13 → 74 | offset 37: `0D → 4A` |
| Encoder 1 Global → channel 15 | offset 34: `01 → 0E`; offset 35: `40 → 00` |
| Encoder 1 Absolute → Relative | offset 35: `40 → 42` |
| Encoder 1 red → blue | offset 32: `05 → 29` |
| Button 1 momentary → toggle | offset 480: `01 → 21` |
| Encoder 1 name → Reverb D1 | length 684 → 693; label starts at offset 293, tag `60 → 69`, then ID and nine text bytes; later bytes shift |
| Button 1 CC → Note (default A3 / MIDI 69) | length 684 → 683; record shrinks by one, type becomes 1, behavior becomes note-specific 1, fixed-velocity flag set, note 69/velocity 127 |

The mixed fixture additionally validates explicit channels 1/15/16, CC 0/127, relative encoder, labels, toggle note 104 (G#6 in Components), Program Change 31, and an off control through Components import/re-export. Physical MIDI emission remains untested.

## Prototype API

`src/devices/novation/launch-control-xl3/codec.ts` has no Vue, browser or Node dependency.

- `createDefaultMode(name?)`: fully specified editable model for the 48 custom controls.
- `encodeFile(mode)`: returns a `Uint8Array`, always using file-export destination 127.
- `decodeFile(bytes)`: returns `{ mode, destination }`; rejects foreign headers, bad framing, duplicate surfaces/records, unsupported record types/flags, missing controls/labels, inconsistent mode names/destinations and truncated data.
- Channels in the model are 1–16, or explicit `'global'` for importing Components' device-channel choice. Elixir templates must resolve their selected channels explicitly rather than use `'global'`.
- `number` is the CC/note/program number. Program trigger writes `number` to both endpoints; increment/decrement write opposing endpoints. `min`/`max` apply to CC values.
- The initial subset is Off, CC7 (absolute/relative), button Note, and button Program Change. NRPN, CC14, HID, non-default continuous pickup behaviors and auxiliary container variants deliberately fail rather than silently import incorrectly. Program increment/decrement are source-derived and round-trip-capable but have not been independently exercised through Components yet.
- For continuous controls, current `behavior: 'momentary'` denotes wire behavior zero (device/default); it does not turn an encoder into a button. Elixir receiving PICKUP/ABS/REL remains a separate concern.

Unlike Components' byte replacement/truncation, our encoder rejects names outside ASCII 32–125 and names longer than 16/31 characters. This makes data loss explicit to the future editor. Complete, ordered controls 16–63 are required. These constraints are intentional prototype limits, not claims about every file accepted by hardware.

## Reproduce

```sh
npm test
npm run build
python3 -m unittest discover -s tools/sysex-diff
node --experimental-strip-types tools/research/generate-xl3-check.mjs
python3 tools/sysex-diff/compare.py src/tests/fixtures/xl3/baseline.syx src/tests/fixtures/xl3/independent-baseline.syx
python3 tools/sysex-diff/compare.py src/tests/fixtures/xl3/independent-mixed.syx src/tests/fixtures/xl3/components-reexport-mixed.syx
```

The comparison tool reports lengths, changed offsets, and frame-local positions. Exit 0 means identical, 1 different, 2 argument/I/O error. `--json` provides the full machine-readable report. Offset comparison is positional; label length changes shift subsequent bytes and are not a semantic alignment algorithm.

To repeat application validation: open Components → Show all products → Launch Control XL 3 → Upload Custom Mode (this means file import, not hardware upload). Choose `independent-mixed.syx`, inspect encoder/fader/button settings, then Save → Download. Compare against the original. The supplied re-export is preserved as a test fixture so future automated tests do not need the proprietary app.

## Remaining evidence

No destination-slot differential was attempted against disconnected hardware. File export fixes the destination to 127; slot-address and DAW-activation translations remain static-source findings. Read-back variants (operations 0x10/0x4A), acknowledgements, LED feedback and firmware variants are not covered by this decoder or the Phase 2 file claim. See [transfer-protocol.md](transfer-protocol.md).
