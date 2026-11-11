# Phase 8 MIDI monitoring and end-to-end validation

Status: complete for the guarded XL3 slot1/build1563/absolute-CC hardware subset.259 tests across13 files and TypeScript/production build pass. Physical global/track channel changes, all six control groups, exact Mixer restoration and unrelated-slot preservation have actual evidence. See `hardware/README.md` for chronology, hashes and limits.

## Monitor

Hardware connection contains a monitor that works without starting raw passive capture. Connect MIDI and choose an input. Choose Expected physical control before moving it for type/channel/number/range comparison. The table shows input name/ID, browser-relative milliseconds, decoded type, channel, number, value, all registry matches, mapped control IDs, comparison and raw bytes.

Shared addresses report ambiguity unless an expected control is selected; matching an address cannot prove which physical control moved. Comparison checks individual events, not temporal button sequences. Registry lookup follows workspace Remote Tracks/Global settings; channel overrides can match assignments without matching the receiving registry namespaces. Note Off normalizes to note value zero while raw bytes preserve release velocity. Program Change shows a program number with no value, real-time has no channel, unsupported channel messages remain `other`, and malformed events `invalid`. SysEx is labeled without interpreting it as performance data.

Pause affects live display only; Clear empties history. Latest200 observations are retained, raw preview capped at16 bytes. Events larger than16KiB are excluded upstream. Mapping edits clear history/reset simulated toggles so old events are not relabeled against new mappings. Uses the existing selected-input subscription/cleanup and sends nothing. Separate bounded raw capture/download preserves evidence beyond the visible200 rows.

## Offline simulator

Select a control and emit an integer value or press/release. No port/output access. Detached resolved mappings model absolute CC ranges/channels, note-on/zero-velocity release, momentary CC buttons, alternating toggle presses and press-only Program Change. Releases on toggle/program triggers emit nothing. Unassigned controls, incompatible actions, out-of-range values and unverified relative output are rejected. This models intended messages, not firmware or Elixir receiving behavior.

## Automated validation

Run `npm test` and `npm run build`. Tests cover all assignments in all five presets on changed channels3/7, channel/address/registry resolution, note/toggle/release behavior, ranges, malformed/system events, relative refusal, ambiguity/mismatch, mounted display, pause/reset and bounded history.

Real evidence regression includes194 Phase7 physical events;281 Phase8 live-monitor events;341 changed-channel physical events across six groups; approved test/restoration traces; Components file import/re-export equivalence; restored Mixer bytes; and all15 before/after slot pairs. Reproduce artifacts with:

```sh
./node_modules/.bin/vite-node tools/hardware/prepare-phase8.ts
./node_modules/.bin/vite-node tools/hardware/phase8-slots.ts --compare
./node_modules/.bin/vite-node tools/hardware/inspect-transaction.ts research/hardware/phase8-test-upload-transaction.json
./node_modules/.bin/vite-node tools/hardware/inspect-transaction.ts research/hardware/phase8-restore-transaction.json
```

Preparation/analysis scripts are offline and send no MIDI.

## Browser and hardware acceptance

| Requirement | Evidence |
| --- | --- |
| Actual monitor displays input/time/type/channel/number/value/parameter/comparison | Chrome live281-event capture; selected encoder correctly flags fader/button address mismatches |
| Desktop and narrow monitor | Chrome screenshots inspected;393×852 emulation,100% scale, focusable table region and keyboard Right reaches final columns |
| Every physical group | First encoder in each row, fader1 and both button rows in saved Phase7/8 captures |
| Track/global channels and regeneration | Temporary test changed15/16 to3/7: Morph CC10/ch7; track CC56/64/8/104/112/ch3;341 saved messages |
| Buttons and encoder format | Momentary127/0; absolute values; fader0–127 in changed-channel capture |
| Export compatible with Components | Changed-channel test accepted local import and equivalent re-export |
| Independent browser upload | Both surfaces accepted, both replies matched and stored read-back passed for test and restoration |
| Restore original mode | Regenerated Mixer restored with15/16; canonical read-back exactly equals pre-test backup |
| Unrelated slots preserved | Components-exported slots2–15 byte-identical before/after the two-write test/restoration sequence |

Two specific slot1 writes were explicitly approved; no other slot was written. Chrome disconnected and Components quit after the final audit. Slot read/export in Components does not validate standalone multi-slot addressing. The all-slot comparison establishes the final state across this sequence, not transient intermediate states.

Remaining scope limits: other firmware/write slots, physical note/program/toggle, relative output, LED feedback, arbitrary backup-import restoration and reconnect persistence after these particular browser writes. Earlier Components-written persistence passed. No end-to-end Elixir receiving claim. Phase9 has not begun.
