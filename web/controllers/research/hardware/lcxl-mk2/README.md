# Launch Control XL mk2 recordings

- `identity.syx` (2026-11-11, the user's unit): its reply to the universal identity request (F0 7E 7F 06 01 F7),
  read with the controllers page's "Read its identity". Novation (00 20 29), family 61 00, member 00 00, version
  bytes 00 00 05 09.
- `a-new-template.syx` (2026-11-11): a new user template as Novation Components exported it, unchanged (the
  user's copy: docs/LaunchControlMk2Template.syx). 417 bytes: `F0 00 20 29 02 11 77 00` (77 = template, 00 = user
  template 1), then 56 controls of 6 bytes (24 knobs CC 13-20/29-36/49-56, 8 faders CC 77-84, 16 buttons notes
  41-44/57-60/73-76/89-92, Device/Mute/Solo/Arm notes 105-108, the four arrows CC 104-107), then 24 knob lights of
  3 bytes (velocities 15 red, 62 yellow, 60 green, 47 orange, in column pairs), then F7.
  Knob: `cc 00 00 7f 00 00` (min 0, max 127?). Button: `type num 00 7f 00 00` (type 00 note, 01 CC; off/on values?).
  Unknown until the variations: the channel, toggle, the zero bytes, and the button lights.
- `c-knob1-channel9.syx`: as (a), knob 1/1 on channel 9. One byte: control 0, byte 4: 00 -> 08 (channel - 1).
- `e-button1-toggle.syx`: made from (c) (knob 1/1 still on channel 9), button 1/1 set to toggle. Two bytes besides
  (c)'s: control 32 (button 1/1), byte 4: 00 -> 01; and the first 3-byte entry after the controls: 0f -> 2f.
- `e2-button1-toggle.syx`: a fresh template, only button 1/1 set to toggle. Control 32, byte 4: 00 -> 01. The 3-byte
  section after the controls also changed (byte 0 of all 24 entries, 00 -> column<<4 | 0x0d..0x0f), differently
  from (e)'s change there: not the toggle. Components' own state, it seems; the writer keeps (a)'s bytes there.
- `f-button1-channel9.syx`: a fresh template, only button 1/1 on channel 9. Control 32, byte 5: 00 -> 08.
- Components had no light color option for the mk2's controls (so no (d)).

The layout, then. Knob and fader: `cc, 0, min, max, channel-1, 0`. Button: `type (00 note, 01 CC), number, off,
on, toggle (01), channel-1`. Exports include unsaved edits (no Novation login needed).
- 2026-11-11, on the user's mk2: the page's Mixer file (made by codec.ts), imported and sent with Components to user template 1, passed every row of the guided check.
- 2026-11-11: sent from the page (Mk2Send.vue: identity, the file, the template select) to the user's mk2: works.
