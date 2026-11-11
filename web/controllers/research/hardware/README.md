# XL3 hardware evidence

## Slot1 backup

User selected custom-mode slot1 for testing. Components reported Launch Control XL3 connected/ready. Its Get Custom Mode →1 successfully retrieved `Elixir Track Mix`. Download saved `slot1-Elixir-Track-Mix-backup.syx` here. This is a read operation; no upload was performed.

- Size:996 bytes.
- SHA256:`a7927bc2feba6b8399a1150ae620a6dd75f9e732a631ccec6c03e394d2118881`.
- Independent decoder accepts both surfaces,48 controls, name `Elixir Track Mix`, export destination127.
- This establishes successful Components read-back and a saved backup; it does not establish our request/ACK parser or backup restoration.
- Original in-memory codec research mode was preserved separately at `/private/tmp/research-mode-before-slot1.syx` before navigating away.

## Capture next step

The passive CoreMIDI utility in `tools/hardware/capture-midi.c` compiles locally. Execution inside the agent's restricted terminal fails at `MIDIClientCreate` with OSStatus-10833. It creates no output port and has no MIDI send operation. Run from a normal macOS terminal:

```sh
cd /Users/dan/code/music/fm-1/midi-mapper
./tools/hardware/start-capture.sh
```

It enumerates and subscribes to MIDI inputs, prints readiness to the terminal and saves incoming packet bytes/source/host timestamp to a uniquely named JSONL file here. Leave it running during another Components slot1 retrieval and a later explicitly confirmed Components upload. Stop with Ctrl-C after capture. Packets may contain partial SysEx; reconstruct complete frames before interpreting. Host timestamps are raw CoreMIDI host-time units, not milliseconds or wall-clock time. The utility does not capture outgoing Components traffic; do not label its files bidirectional traces.

Firmware identity, ACK shape/status/correlation, exact hardware slot bytes, persistence and physical output remain unverified. Slot choice alone has not been treated as the promised final overwrite confirmation.

## Captured read-back and prepared upload

User started the native passive capture in their terminal and confirmed readiness. With capture running, Components Get Custom Mode →1 retrieved `Elixir Track Mix` again. `input-20261110-133706.jsonl` contains26 CoreMIDI packets at this stage; it is a live file and may grow. `tools/hardware/extract-capture.py` reassembled two immutable raw frame snapshots under `slot1-readback/`:

- Surface0:515 bytes, header `f0 00 20 29 02 15 05 00 10 00 06`.
- Surface3:451 bytes, header `f0 00 20 29 02 15 05 00 10 03 06`.
- Source: `LCXL3 1 MIDI Out`. DAW input also emitted9 bytes outside these SysEx frames.

This establishes actual incoming operation0x10 and byte10=0x06 for this retrieval of human slot1. It does not establish that byte10 is a zero-based destination; do not use the file-export decoder or infer write addressing directly from it. Hardware replies also differ in record containers and trailing metadata from our canonical file format. Preserve raw evidence before extending parsing.

Generated `elixir-mixer-test.syx` using `npm run template -- mixer 15 16 ...`; semantic validation reports48 assignments, no issues, no unassigned controls. Imported it into Components using Upload Custom Mode (local file import, not a device send). Components displays `Elixir Mixer`, D1 Reverb send CC72, and the expected other controls. The original slot1 backup remains unchanged. No upload has occurred. Await explicit overwrite confirmation before sending this prepared mode to slot1 through Components while capture is running.

## Confirmed Components overwrite and subsequent read-back

User explicitly approved overwriting slot1 with the prepared `Elixir Mixer` mode. Selected1 in Components Send → Select Custom Mode, then clicked Overwrite Custom Mode. Components showed Sending and returned to ready. The capture recorded:

```
f0 00 20 29 02 15 05 00 15 00 06 f7
f0 00 20 29 02 15 05 00 15 03 06 f7
```

These are observed replies to the two successful Components surface writes for human slot1. The operation is0x15, surfaces0/3, final data byte0x06. No independent status field or error response has been established. Do not infer other slots' values or strict cross-device correlation from a single transaction. Outgoing writes were not captured.

A fresh Get Custom Mode →1 showed `Elixir Mixer`. Export saved `slot1-Elixir-Mixer-readback.syx`, SHA256 `f0f8b49c420d9ce2a15aa6b04d2f0dfc0fad6458b817b89dbdafea7046b10cb2`. Compared with the original generated test file (SHA256 `3278b134e16461d1fa70cfb240cf2b9fcccd5dd10bc2674a159e9283c76bb6cb`), all decoded fields match EXCEPT encoders/faders IDs16–47 return `continuous:true` rather than false. This observed normalization is explicitly tested, not hidden as a byte-exact match. No serializer behavior was changed based on this single observation.

Raw replies and post-write read responses are preserved in `slot1-upload-and-readback/` (frames3/4 replies,5/6 post-write surfaces; frames1/2 are the earlier original mode). Reassembly is reproducible from the live input JSONL using `extract-capture.py`. Before/after response lengths are515/451 and652/546 bytes respectively.

Verification status:
- Components upload: completed in the UI.
- Replies: observed for both surfaces during the approved write.
- Stored state: fresh Components read-back returns matching assignments with the documented flag normalization.
- Persistence after power/reconnect: pending.
- Physical output: requested encoder1, fader1, mute1, solo1 actions; pending capture review.
- Our standalone uploader: still disabled; it did not perform this transfer. Firmware identity, independent outgoing write addressing, error/status behavior and unrelated-slot preservation remain open.

226 automated tests across nine files and the production build pass, including regression checks for the exact observed reply fixtures and the explicit read-back difference. These tests preserve evidence; they do not substitute for pending physical/persistence checks.

## Representative physical output — passed

User reported completing the requested encoder1, fader1, first mute and first solo actions with custom mode1 active. Snapshotted573 incoming three-byte CC packets from the live log (zero-based packet66 onward) into `slot1-physical-controls.jsonl`; summary in `slot1-physical-summary.json`. Snapshot SHA256:`fc5fbe67727c7e324935a5880b351d5ddedeb99c7447ba64e3dc150176d35791`.

| Physical action | Channel | CC decimal | Observed values | Count |
| --- | --- | --- | --- | --- |
| Top-left encoder | 15 | 72 | Full0–127 range | 139 |
| Fader1 | 15 | 8 | Full0–127 range | 430 |
| Button row1 column1 (mute) | 15 | 104 | Press127, release0 | 2 |
| Button row2 column1 (solo) | 15 | 112 | Press127, release0 | 2 |

All packets use status0xBE; every number, channel and value matches the generated Mixer assignments. Continuous output is absolute in this test. This checks four representative controls only; it does not verify relative output, other controls or end-to-end Elixir receiving/LED behavior. Asked user to unplug/reconnect and select mode1 next to check persistence. The native capture subscribes to endpoints present at startup; endpoint replacement may require restarting it.

## Persistence after unplug/reconnect — passed

User confirmed unplugging/reconnecting the XL3 and selecting custom mode1. Components reported ready. Performed a fresh Get Custom Mode →1 (no writes), then saved `slot1-Elixir-Mixer-after-reconnect.syx`. It is byte-for-byte identical to `slot1-Elixir-Mixer-readback.syx`, SHA256 `f0f8b49c420d9ce2a15aa6b04d2f0dfc0fad6458b817b89dbdafea7046b10cb2`. This verifies persistence of this tested slot/configuration across the user's reconnect.

The running native capture continued receiving after reconnect. `slot1-reconnect-capture.jsonl` snapshots the universal identity reply and the subsequent read-back packets, excluding the separate device serial-number response. Raw reassembly in `slot1-reconnect/` includes the identity reply:

```
f0 7e 00 06 02 00 20 29 48 01 00 00 01 02 0c 1b f7
```

Manufacturer bytes00 20 29; remaining family/member/version fields preserved verbatim. Do not confuse the universal-identity family bytes48 01 with the proprietary protocol product bytes02 15. Firmware numeric interpretation still needs cross-checking against Components' identity/version decoding; no unverified marketing version is claimed. Physical/control persistence results apply to this device/slot/test only. The standalone upload gate remains closed pending implementation/verification of its own identity/protocol path; outgoing Components write bytes were not captured.

## Actual standalone browser identity/read-back/backup — passed

User started Vite outside the sandbox and supplied `http://localhost:5173/`. Used Chrome's native UI (browser automation host unavailable). Closed Components to avoid competing requests. Clicked Connect MIDI, accepted the expected local SysEx permission, and selected browser input `LCXL3 1 MIDI Out` / output `LCXL3 1 MIDI In` (Focusrite - Novation). These labels reflect device direction, so the browser's input is named Out. DAW and network ports were not selected.

Clicked Verify device & back up slot1. The standalone implementation successfully performed identity enquiry, strict application/build1563 validation, both correlated operation0x10 surface requests and CC-subset parsing. UI reported `Identity checked; both surfaces backed up. No write performed.` and current slot `Elixir Mixer`.

Downloaded through the native Chrome Save dialog to `slot1-browser-backup.syx`. It is byte-for-byte identical to `slot1-Elixir-Mixer-after-reconnect.syx`, SHA256 `f0f8b49c420d9ce2a15aa6b04d2f0dfc0fad6458b817b89dbdafea7046b10cb2`. This establishes real browser permission, input/output pairing, request/reply delivery, identity, hardware read-back parsing and backup download. No standalone hardware write occurred. Read request uses zero-based slot0; observed reply address remains6 and is separately validated. This confirms human slot1 read addressing without confusing it with DAW selection addressing.

Inspected desktop Chrome screenshots: setup fields, controller/editor columns and hardware panel render legibly without visible overlap. Narrow/mobile rendering and the full editing/import/export UX are not yet visually accepted; do not treat this desktop check as full Phase6 visual QA.

## Approved direct browser upload — send, replies and read-back passed

User explicitly approved testing direct browser upload of Elixir Mixer to slot1. Enabled the narrowly restricted slot1/build1563 absolute-CC path; arbitrary slots/firmware/types remain rejected. Reconnected through the app, re-verified identity, read both surfaces, and downloaded `slot1-before-direct-upload.syx` (byte-identical to the previous verified backup). Checked the overwrite confirmation and clicked Confirm overwrite & upload. No retry or second write was performed.

The actual browser reported2/2 accepted surface sends,2/2 matching replies, and matching read-back. Saved the complete bidirectional log as `slot1-direct-upload-transaction.json` (20 entries, not truncated), SHA256 `57eff3871631a9924f66d7766286d9d1be88f3d45d3bc7ac3b1583b22f5f0118`.

Transaction sequence:

- Requests1–3: identity and both pre-confirmation backup surfaces.
- Requests4–6: fresh identity and slot-content recheck immediately before write.
- Request7: encoder write,670 bytes, header `f0 00 20 29 02 15 05 00 45 00 00`;12-byte reply `f0 00 20 29 02 15 05 00 15 00 06 f7`.
- Request8: fader/button write,558 bytes, header `f0 00 20 29 02 15 05 00 45 03 00`;12-byte reply `f0 00 20 29 02 15 05 00 15 03 06 f7`.
- Requests9–10: both stored surfaces, strict operation0x10/surface/address6 correlation, decoded comparison passed.

This establishes actual standalone outgoing write index0 for human slot1, separately from observed response address6. The sent data equals the independent generated test file with only destination127 replaced by0 in each frame. Reconstructed post-write canonical file saved as `slot1-direct-upload-readback.syx`, byte-identical to the pre-write Components/browser backup and with the previously documented continuous flag normalization relative to generated input. `tools/hardware/inspect-transaction.ts` reproduces this comparison and optional canonical export. Trace timestamps are browser wall-clock milliseconds, request IDs local to one scoped transport; trace logs only browser-accepted sends and incoming messages during each request.

Browser passive capture was started for a short post-direct-upload physical check. Asked user to move encoder1/fader1 slightly and press/release mute1/solo1. These new physical checks and reconnect persistence after the independent write remain pending. The earlier Components-written version passed both, but those results are not substituted for the new direct-write checks.245 tests across12 files pass, including immutable actual-transaction regression checks. No general error/status or other slot/firmware compatibility is asserted.

## Post-direct-upload physical output — passed

User confirmed completing the requested short physical check. Stopped browser passive capture and downloaded194 messages as `slot1-direct-upload-physical.json`. Every message is a channel15 CC (status0xBE):

| Control | CC | Count | Observed range / sequence |
| --- | --- | --- | --- |
| Encoder row1/column1, Reverb send | 72 | 25 | 1–45 |
| Encoder row2/column1, Delay send | 56 | 21 | 1–32 |
| Encoder row3/column1, FX send | 64 | 17 | 1–24 |
| Fader1, volume | 8 | 127 | 1–127 |
| Mute1 | 104 | 2 | 127,0 |
| Solo1 | 112 | 2 | 127,0 |

All assignments/channels and momentary press/release behavior match Mixer after the standalone upload. This sample validates representative output, not all48 controls or a new complete0–127 sweep of each continuous control. The two extra send encoders were also exercised and match. No more writes or user actions were performed. Reconnect persistence after this independent write remains untested; earlier persistence evidence applies to the Components-written equivalent mode.

The live path remains limited to slot1/build1563/absolute CC. Backup restoration and unrelated-slot preservation remain untested.247 tests across12 files and production build pass. UI status and README now describe the verified subset rather than the earlier globally disabled upload gate. Browser transaction logs bind metadata to the original input/output/mapping/slot even if the workspace later changes.

## Phase 8 regression use

The new monitor replays all194 saved direct-upload physical events in automated tests; each matches the Mixer address/range and resolves an Elixir parameter, covering six control groups. This adds reproducible analysis, not a new live capture. Physical global-channel/changed-channel output, new monitor browser acceptance and unrelated-slot preservation are still open. See `../midi-monitor.md` for the Phase8 validation checklist. No additional hardware operation was performed.

## Phase 8 actual live monitor — passed

User completed encoder1/fader1/mute1/solo1 movements with mode1 selected. The new Chrome monitor received live messages from `LCXL3 1 MIDI Out` (input ID87413844). Stopped passive capture and paused the monitor, then saved `phase8-slot1-monitor-physical.json` through Chrome's native Save dialog. SHA256: `25d22c76483ac491e42290eef0d45e557df1369bb50f52c2036177a953566aaa`.

281 messages, all absolute CC on channel15:

| Control | CC | Count | Values |
| --- | --- | --- | --- |
| Encoder1/1 | 72 | 67 | 8–94 |
| Fader1 | 8 | 210 | 13–127 |
| Mute1 | 104 | 2 | 127,0 |
| Solo1 | 112 | 2 | 127,0 |

Live UI showed input name/ID, browser timestamp, CC/channel/number/value, D1 Volume/Mute/Solo parameter names and raw bytes. Expected control was encoder1/1: fader/button observations correctly displayed `address mismatch` while identifying their actual mapped controls. The latest200 monitor rows exclude the earlier encoder events; the separate raw capture preserves all281. Regression replay verifies all281 against their mapped addresses and the selected-encoder match/mismatch expectations. No output port was selected, no query/write occurred and no new firmware verification is claimed for this passive session. This accepts representative live input and expected-control mismatch display. Global/changed-channel physical output, unrelated-slot preservation and narrow-layout acceptance remain open.

## Phase 8 final-test preparation

All15 current custom modes were retrieved using Components' own read-only Get Custom Mode UI, then exported as `phase8-before-slot01.syx` through `phase8-before-slot15.syx`. Their SHA256/lengths are recorded in `phase8/slot-manifest.json`. Slot1 still matches the earlier Elixir Mixer export byte-for-byte. The standalone protocol remains slot1-only; these Components exports do not validate standalone read/write support on other slots.

`phase8/channel-check.syx` and `.json` contain reviewed mode `Phase8 Ch3 Ch7`: Mixer track assignments on channel3, with encoder1/1 global Morph CC10 on channel7. Components accepted local import and `phase8/phase8-channel-check-components.syx` re-export matches independent decoded/re-encoded data. No hardware send occurred. Chrome loaded the JSON and performed fresh identity/build1563/slot1 backup; saved `phase8-before-browser-slot01.syx`. Overwrite remains unconfirmed. Proposed test is this one slot1 overwrite, physical observations across six groups, restoration of Elixir Mixer, then fresh Components exports of unrelated slots2–15 and byte comparison. No other slot is a write destination.

## Phase 8 final acceptance — passed on slot1/build1563/absolute CC

User explicitly approved the temporary slot1 test and subsequent Mixer restoration. Direct Chrome upload of `Phase8 Ch3 Ch7` passed2/2 accepted sends,2/2 matching replies and stored comparison. Full trace `phase8-test-upload-transaction.json` (SHA256 `069669bdad5877ad64c137c21e01beb4bec3e6308a0fa3cbfd88ffe407c9ecbc`), canonical `phase8-test-readback.syx`. Write lengths663/560,index0.

User completed all six requested physical groups. Stopped capture/paused monitor and saved `phase8-channels-physical.json`,341 events, SHA256 `4eb82c548f8a8ca065cf888ec8fc38864b031ff57275896172b9764ecfcfe9e3`:

| Control | Channel | CC | Count | Observed values |
| --- | --- | --- | --- | --- |
| Encoder1/1, global Morph | 7 | 10 | 45 | 1–110 |
| Encoder2/1, D1 Delay send | 3 | 56 | 49 | 1–90 |
| Encoder3/1, D1 FX send | 3 | 64 | 45 | 1–85 |
| Fader1, D1 Volume | 3 | 8 | 198 | 0–127 |
| Mute1 | 3 | 104 | 2 | 127,0 |
| Solo1 | 3 | 112 | 2 | 127,0 |

This verifies physical global and track namespace channel resolution after changing defaults15/16 to3/7, absolute output and momentary behavior. Every event resolves to the intended Elixir parameter; all six groups are covered. No Elixir receiving behavior is inferred.

Loaded Mixer in Chrome, explicitly restored channels15/16 (preset selection retains workspace channels), verified identity and backed up test mode as `phase8-before-restore-slot01.syx`. Executed the already-approved Mixer restoration with fresh one-use confirmation and pre-write rechecks. Both accepted sends/replies and stored comparison passed. Trace `phase8-restore-transaction.json`, SHA256 `b8c39fe2424473c31953042f6335733daa55df8d6c8fe9c36501f49dddf8eebf`; write lengths670/558,index0. Reconstructed `phase8-restored-readback.syx` is byte-identical to `phase8-before-slot01.syx`, original Mixer SHA256 `f0f8b49c420d9ce2a15aa6b04d2f0dfc0fad6458b817b89dbdafea7046b10cb2`. This verifies exact stored restoration of this known Mixer configuration, not a general backup-import UI.

Disconnected Chrome MIDI. A fresh Components session retrieved/exported all15 slots after the two approved browser writes. `phase8-after-slot01.syx` through `phase8-after-slot15.syx` all match their corresponding pre-test exports byte-for-byte, including restored slot1 and all14 unrelated slots. Reproduce using `./node_modules/.bin/vite-node tools/hardware/phase8-slots.ts --compare`; full hashes in `phase8/slot-manifest.json`. This demonstrates no net change to unrelated slots across this test/restoration sequence, not all possible writes or interrupted-transfer behavior. Components remained read-only during the audit and was quit after exporting; browser is disconnected.

Operational correction: the earlier request to quit Components before the test left its native leave-confirmation dialog open. This was noticed after restoration, confirmed closed, and Components reopened cleanly for the post-write audit. No competing Components transaction was initiated during either browser upload, and the complete correlated traces/read-backs passed. Future automation must verify application exit after the quit prompt.

Phase8 acceptance now passes for the existing guarded subset.259 automated tests in13 files and TypeScript/production build pass, including actual changed-channel capture, both transaction traces, exact restoration and every before/after slot pair. Remaining limits are broader firmware/slot write support, note/program/toggle physical acceptance, relative/LED feedback, arbitrary backup import and reconnect persistence after these particular browser writes. These do not expand the live-upload policy. Narrow monitor layout was accepted at393×852 Chrome emulation, with visible scroll guidance and keyboard access to all table columns.
