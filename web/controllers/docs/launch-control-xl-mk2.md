# Launch Control XL mk2: what's supported, and the recordings that add the rest

The mk2 has the XL 3's layout, so the mapper draws it, edits it with the same templates, and checks it. The template
moves between the two models (`src/devices/families.ts`). Three differences:

- **Knobs:** potentiometers with end stops, absolute only. On Elixir that means PICKUP.
- **Lights:** red, amber, yellow and green. The colors here are the mk2's LED velocities, from Novation's
  programmer's reference: 12 off, 15 red, 63 amber, 62 yellow, 60 green.
- **Buttons:** CC or note only. The patterns (program changes) are left off the mk2's buttons.

**Not supported yet:** writing a template (device file or upload). The format Novation Components writes for the mk2
isn't recorded here. Until it is, the page lists the template (Setup → "The template as a list") for setting it up
in Components by hand. "Connect & check" shows what each control sends, and the page sends the controller nothing,
except the identity question when asked.

## Recordings

Each step changes nothing on the controller unless it says so. Send the files and text back.

1. ~~**Its identity.**~~ Done 2026-11-11: `f0 7e 00 06 02 00 20 29 61 00 00 00 00 00 05 09 f7`, family 61 00
   (`research/hardware/lcxl-mk2/identity.syx`). `identifyModel` knows it.
2. **A check** (later: once the page can write the mk2's template, rather than setting 43 controls in Components by
   hand). Load the template, move every control in the "Check it" panel, and note any line without a ✓.
3. **The template format.** This step writes templates, but only from Components, as you normally would.
   - If Components can export an mk2 template to a file, export these. Otherwise record what Components sends with
     MIDI Monitor (snoize.com, macOS; "Spy on output to destinations", the Launch Control XL), and save each send as
     a .syx file.
   - The files, each one change from (a), so each field can be found:
     - a. user template 1, all as Components makes a new template
     - b. as (a), knob 1/1 set to CC 77
     - c. as (a), knob 1/1 on channel 9
     - d. as (a), knob 1/1's light amber
     - e. as (a), button 1/1 as a toggle
     - f. as (a), button 1/1 sending a note (60)
     - g. (a) sent to user template 2
   - If Components reads the templates back from the controller, record that too, with MIDI Monitor on its source.

With those, an mk2 serializer can be written and tested against the files, like the XL 3's
(`research/hardware`). Uploading from the page would come last, behind the same gates: identity, a backup read
first, confirmation, and one slot verified on hardware.

## Status (2026-11-11)

Decoded from the user's exports (research/hardware/lcxl-mk2/README.md): the page writes the mk2's template file
(`codec.ts`, `adapter.ts`), byte for byte as Components does for each recorded change. Components has no color option
for it, so the mk2 has no colors here. Checked on the user's mk2 the same day: the Mixer's
file, imported and sent with Components, passed the guided check (Connect & check). Upload from the page: not built;
Components sends the file.
