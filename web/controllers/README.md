> **In Elixir's repository** (1.3.2): this is the site's `controllers/` page. `web/make_site.py` builds it (npm) after
> generating `data/elixir-midi-parameters.json` from the firmware (`tools/remote_params.py`): the registry has no
> hand-kept table (`src/elixir/parameter-registry.ts` loads the generated JSON via `vite.config.ts`). Here:
> `npm ci && npm test` (the tests generate the registry themselves, with python3), `npm run dev` for a live preview.
> `integration/` is the standalone project's hand-over notes; `research/hardware` the captures its tests replay.

# Elixir MIDI Mapper

Standalone, client-only Vue 3 / TypeScript controller mapper. Generate and edit five XL3 and three Launchpad Pro MK3 Elixir templates, export `.syx` or editable JSON, and connect an XL3 for device verification, backup and guarded direct upload. Real browser upload, both surface replies, read-back and representative physical output have passed on firmware build1563, custom-mode slot1, absolute CC mappings. Other slots/firmware and note/program/relative hardware transfers are blocked pending validation.

## Run

Use Node.js 24.15+ and npm to satisfy all locked dependency engine requirements. Tests and build also pass on the available Node 24.2, with engine warnings from test-tool formatting dependencies.

```sh
npm ci
npm run dev
npm test
npm run build
npm run preview
```

Open the localhost URL printed by Vite. Production output is `dist/`; serve it with any static host. Relative asset URLs support GitHub Pages subdirectories including `/elixir/`. No backend, Electron, external bridge, or Elixir checkout is needed. Navigation currently uses local component state, so it needs no server route fallback.

## Boundaries

- `src/components/MidiMapper.vue`: embeddable mapper interface; no router or global store.
- `src/midi/provider.ts`: framework-independent provider contract, mock provider, and non-prompting feature detection.
- `src/core/mapping-engine/`: portable semantic mapping, validation, JSON import/export and serialization interfaces; see `research/mapping-engine.md`.
- `src/elixir/`: complete documented remote parameter registry and configurable-channel APIs; see `research/elixir-midi-map.md`.
- `src/devices/novation/launch-control-xl3/codec.ts`: independent file-only encoder/decoder; supported subset and API in `research/sysex-format.md`.
- `src/tests/`: provider/component tests, protocol validation, and genuine Components file fixtures.
- `tools/sysex-diff/compare.py`: binary comparison CLI; verify it with `python3 -m unittest discover -s tools/sysex-diff`.
- `research/progress.md`: phase gates, evidence, and outstanding work.
- `reference/`: user-supplied research material; never a runtime dependency.

Vue is an implementation choice, not a hosting requirement. Future mapping, registry, serialization, and transport modules must remain usable without Vue. Keep proprietary reference code/assets out of application source and production output.

Hardware access uses desktop Chrome/Edge, HTTPS or localhost, and explicit SysEx permission. No MIDI permission is requested on page load. Selecting a port name alone never verifies device compatibility; identity and firmware are checked before read-back or upload.

The separate Elixir website must not be modified here. The portable embedding API and Claude Code handoff are in [integration/README.md](integration/README.md).

## Restricted development environment

The current environment prevents npm registry DNS access but permits approved curl downloads. Dependencies were fetched into ignored `tmp/npm-downloads/`, checked against registry integrity hashes, and cached locally. With that cache present, reinstall using:

```sh
npm ci --offline --cache ./tmp/npm-cache --no-audit --no-fund
```

The cache is temporary and not part of the deliverable. On an ordinary network, use `npm ci` with the committed lockfile. This environment also blocks localhost port binding (`listen EPERM`); start `npm run dev` from an unrestricted local terminal to view the app.

## Simple workspace and admin tools

The default page is a compact, viewport-height mapper with an Elixir-style warm dark/amber theme. Choose Launch Control or Launchpad in the sidebar, set track/global channels in the top bar, and use Tracks/Global tabs plus the category dropdown to browse parameters. Continuous controls and pad/button actions are grouped separately. Drag onto the device, or select a parameter and click its destination. Config contains immediate LED colors, clear assignment, row fill, presets and JSON save/open. Download .syx from the top bar.

The full interface is preserved in `src/components/AdminMidiMapper.vue`: open `?view=admin`, or use **Open admin tools** in Config. Hardware connection, monitor, simulation, detailed validation and advanced editing live there. The two views have independent workspaces; save/open JSON to move edits. The new compact component is `src/components/MidiMapper.vue` and is the default public export. The instructions below describe admin tools where they mention advanced panels.

## Mapping workflow

Choose a preset or **New blank template**, click a control, select a compatible parameter, and press **Apply assignment**. Configure matching Elixir remote channels and receiving modes, then **Download .syx**. **Download template JSON** preserves editable user mappings; **Import template JSON** restores them. In-session saves clear on reload. Template selection replaces the workspace, so save edits first.

CLI inspection/export: `npm run template -- mixer` or `npm run template -- effects 15 16 /tmp/elixir-effects.syx`. See [template layouts and editing APIs](research/templates.md).

## Launchpad Pro MK3

Choose **Launchpad Pro MK3** for a64-pad preview with Mute/Solo,32-pattern selection and Performance presets. Pads support CC, note and Program Change; selected remote channels flow through edits, exports and simulation. Generated single-pad `.syx` files have been imported and re-exported through Components. Components uses the imported filename as the mode name; all other setting bytes match. Surrounding navigation buttons, fader widgets, continuous macros and voice triggering are outside this initial subset.

Direct browser upload, identity/build483, stored read-back and all16 Mute/Solo pads passed on slot1. All32 physical CC events used channel15 with127 press/0 release. Browser restoration recovered Drums Square byte for byte. Live upload supports slot1/build483 and explicit-channel momentary CC/off pads. Quick-tap and release Custom, then quick-tap and release the leftmost Track Select button immediately below the grid to select mode1. Other slots/builds, note/program/toggle hardware transfers, reconnect persistence and unrelated-slot preservation await validation. LED colors configure local pad appearance; external Elixir feedback is unverified. Read [Phase9 evidence and limits](research/launchpad-pro.md) and [file fixture provenance](src/tests/fixtures/launchpad-pro-mk3/README.md).

Use **Prepare MK3 slot 1 upload**, download the fresh backup, review and confirm the overwrite. The app rechecks device identity and original slot contents, sends once, and compares the stored mapping. **Restore original MK3 slot 1** restores the original saved in that browser session and verifies its stored bytes. Keep the downloaded backup if leaving the page. [Hardware captures and hashes](research/hardware/launchpad-pro-mk3/README.md).

## XL3 hardware workflow

1. Close Components to avoid competing requests. Click **Connect MIDI**, grant SysEx permission and select the main XL3 input/output, excluding DAW/DIN/network ports. On macOS the browser input may be named `LCXL3 1 MIDI Out`, and output `LCXL3 1 MIDI In`.
2. Keep slot1 selected. Click **Verify device & back up slot 1**, then **Download slot 1 backup**. This checks the captured application identity/build1563 and reads both surfaces without writing.
3. Review the mapping and overwrite warning, tick confirmation, then **Confirm overwrite & upload**. This rechecks identity and original slot contents, sends surfaces sequentially, validates both replies and compares read-back. Device/mapping/slot changes invalidate preparation and confirmation.
4. Keep the backup and **Download transaction log**. The log distinguishes browser-accepted sends, incoming replies and stored comparison. Physical output and reconnect persistence are separate checks; passive capture supports input inspection.

Live upload currently accepts only slot1, the captured build1563 identity and absolute explicit-channel CC/off mappings. File export supports the broader verified codec subset, including note and Program Change. No automatic retries, firmware updates or DAW activation are performed. Unknown replies time out safely; other slots, firmware, relative output and LED feedback are unverified. Reconnect persistence passed for the Components-written mode; it has not been repeated after the independent browser write. Phase8 verified changed track/global channels, restored the original Mixer byte-for-byte, and confirmed all14 unrelated slots unchanged across the test/restoration sequence. Arbitrary backup import/restoration is not a standalone UI feature.

See [transfer protocol and limits](research/transfer-protocol.md), [hardware evidence](research/hardware/README.md) and [phase progress](research/progress.md). Phase8 is complete for the supported hardware subset;259 tests and the production build pass. Desktop and393px monitor layouts, native backup download and actual live monitoring were inspected. Broader full-editor visual acceptance remains separate. See [monitor/simulator validation](research/midi-monitor.md).

## MIDI monitor and offline simulation

The Hardware connection panel includes a decoded monitor. Connect and select an input; choose an expected physical control to compare channel/type/number/range. Pause or clear the latest200 observations; raw passive capture remains available for JSON evidence. A matching address cannot identify a physical control when assignments share addresses. The table scrolls horizontally and supports keyboard focus/arrow keys.

Simulation requires no MIDI connection and sends nothing. Choose a control and emit an absolute value or press/release to inspect the template's channels, CC/note/program messages, ranges and toggle state. Relative simulation is refused until its value format is verified. Mapping changes clear history and reset simulated toggles.

## Portable integration (Phase 10)

Import `MidiMapper` and its types from `src/integration/index.ts`, and import `src/style.css` once. Optional mount configuration supports parameter overrides, default channels, enabled built-in devices, saved templates and theme settings. Detached `save-template` and `template-change` events let the host manage persistence. See the [self-contained Claude integration plan](integration/CLAUDE_INTEGRATION.md) for the copy inventory, API, dependencies, Pages configuration and acceptance checks. The host website has not been modified or validated.

## Fast assignment and parameter groups

In the admin view, use **Tracks · ch 15** or **Global · ch 16** in the parameter browser (labels follow your configured channels). Tracks view shows each per-track family once, so **FX send** represents all seven tracks. Control type filters distinguish continuous knobs/sliders from button/pad actions; the default shows compatible parameters.

Drag a parameter onto a control to apply it immediately. With **Auto-fill tracks 1–7** enabled, dropping a track family on column 1 replaces that row with D1/D2/D3/SN/CY/CH/OH at the registry's CCs and remote-track channel. Column 8 becomes master volume for continuous rows, or unassigned for action rows. There is no generic master FX send or master mute/solo in the documented map. Other rows and existing presets are unchanged. Turn auto-fill off for a single assignment, or select a parameter and use **Apply across tracks 1–7**. Incompatible replacements leave the workspace unchanged. Row fill replaces row-specific overrides/colors with parameter defaults.

LED selectors use named colors and a clickable swatch palette. Swatches approximate the hardware palette; use **Apply assignment** to commit a color edit. Assignment/export still uses the device's original numeric palette values.

## Send and configuration files

The compact top bar includes **Send to Device**, **Download** and **Import**. Download offers a device `.syx` export or a fully editable mapper `.json` config. Import accepts that JSON format, restoring controller, assignments, colors, channels and saved destination slot. Arbitrary `.syx` files cannot be imported into the semantic editor; keep JSON for editing and use `.syx` with the controller/Components.

Send to Device opens the existing guarded transfer flow without leaving the compact workspace. Launch Control XL3 has15 custom modes; Launchpad Pro MK3 has8. The selector lists all modes and saves your selection in JSON, but direct transfer remains restricted to verified mode1/firmware/message subsets. Other modes require `.syx` download and destination selection in Components. Connect MIDI explicitly, choose main input/output, verify/back up the original, download the backup and confirm the overwrite. No permission prompt or send occurs merely by opening the panel. The close button/Escape are blocked while an operation runs; use its Cancel operation action if necessary. Closing the panel retains its session while the same device remains selected; changing device/reloading discards it, so keep the downloaded backup.
