# XL3 file fixtures

Created 2026-11-10 using installed Novation Components 1.72.5 and our independent codec. No hardware was connected or programmed. `manifest.json` records lengths, SHA-256, origins, settings and baseline-relative byte differences.

- `baseline.syx`: genuine app export from New Custom Mode, with its default `*New Custom Mode` name and all 48 default assignments.
- `encoder-cc74`, `channel15-verified`, `encoder-relative`, `encoder-blue`, `label-isolated`, `button-toggle`, `button-note`: app exports with one isolated UI setting changed from baseline. Note type selection also changes the record's applicable fields/length.
- `encoder-label.syx`: an intermediate app export with BOTH relative encoder and label changes; retained with accurate provenance, not used as an isolated label probe. Components Reset did not clear the relative flag, so Absolute was explicitly restored before `label-isolated.syx`.
- `independent-baseline.syx`: independently generated, identical to genuine baseline.
- `independent-mixed.syx`: independent mode named Elixir codec; encoder 1 CC0/channel1/relative/Reverb; fader1 CC127/channel16/Volume; button1 note104/channel15/toggle; button2 Program Change31/channel16/trigger; button16 off. Other controls retain defaults.
- `components-reexport-mixed.syx`: actual app export after importing the independent mixed file; byte-identical to the independent original.

Genuine baseline was exported via Save → Download into this directory and renamed locally from an awkward save-dialog filename; its bytes were not edited. Two early UI automation probes failed to apply the intended CC/channel changes. Those files were moved to ignored `tmp/rejected-probes/` and are not reference fixtures. Every retained isolated probe is checked against an independently constructed semantic model in unit tests.

These are newly created configuration data, not extracted factory packs or proprietary application source/assets. Files must not be sent directly to physical hardware based solely on file compatibility.
