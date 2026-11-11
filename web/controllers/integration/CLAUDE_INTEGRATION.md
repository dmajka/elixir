# Claude Code implementation plan: Elixir MIDI Mapper

## Goal and boundaries

Add a MIDI Mapper navigation option to the existing Elixir website and mount the supplied Vue 3 interface. Inspect the host repository's instructions and architecture first. Preserve existing pages, audio behavior and MIDI consumers. Source is the standalone `midi-mapper` project containing this document; do not copy its `dist/` over the host application. Integration has not been executed against the host repository.

## Required parameter source update

During integration, implement [PARAMETER_JSON.md](PARAMETER_JSON.md): every website build must generate and publish a complete parameter JSON from Elixir's authoritative firmware definitions, validate it, and automatically load those same records as the mapper's registry. Expose **Download parameter JSON** in the site menu; CSV is an optional secondary export generated from that JSON. Keep parameter-registry JSON distinct from editable controller-config JSON. This replaces reliance on manually copied cheat-sheet CC values in the integrated site. Include a source revision/hash manifest and tests demonstrating that a changed CC propagates into UI, exports and simulation. This is required integration work, not an already implemented standalone feature.

## Copy inventory and destination

Suggested destination (keep relative source layout intact):

```text
<host>/src/features/midi-mapper/
  integration/       # public index.ts, context.ts
  components/        # all seven .vue files
  core/mapping-engine/
  elixir/
  devices/novation/launch-control-xl3/
  devices/novation/launchpad-pro-mk3/
  midi/
  style.css
```

Copy **all** `.ts` and `.vue` files under those source directories. Required components: MidiMapper, AdminMidiMapper, ControllerPreview, ParameterBrowser, HardwareConnection, MidiMonitor, LaunchpadTransfer (seven components). `core/mapping-engine` contains types, schema, engine and exports. `elixir` contains registry, channel settings, template catalogs/generators and engine composition. Both device directories contain profile, codec, adapter and hardware protocol/upload modules; XL3 additionally has transfer and hardware-policy modules. `midi` contains provider, web-midi, request-transport and monitor. The `integration` context supplies a per-mount registry/engine to descendant components through Vue injection.

Copy `src/tests/` including both fixture trees to a corresponding test directory, preserving relative imports or adapting the test root. Retain fixture provenance READMEs. Also retain relevant research evidence/limits in project documentation (especially `launchpad-pro.md`, `transfer-protocol.md`, `midi-monitor.md`, `sysex-format.md`, `elixir-midi-map.md`). Do not copy `reference/`, `research/extracted/`, `tmp/`, `node_modules/`, standalone `main.ts`/`index.html`, hardware tools or research assets into the production source. No runtime assets are needed.

## Dependencies and build

Runtime dependency: Vue 3.5. The source uses Vue SFCs, TypeScript, ES modules, DOM APIs and structuredClone. Reference toolchain: Vite 7, @vitejs/plugin-vue 6, TypeScript 5.9, vue-tsc 3.1, Vitest 3.2, @vue/test-utils 2.4 and jsdom 27. Use the supplied package.json and package-lock.json for exact standalone resolution. Use Node 24.15+ to meet all dependency engine requirements. Merge into the host toolchain; avoid creating a second Vue runtime.

`MidiMapper` is the default compact interface. `AdminMidiMapper` preserves the full diagnostic/editor interface. Standalone `main.ts` selects admin with `?view=admin`; the host can mount either exported component using its own navigation. The Config admin link is a standalone convenience; adapt it to host navigation during integration. Each mount owns its workspace; download/open JSON to transfer edits. Hardware sessions never transfer between views.

Standalone verification:

```sh
npm ci
npm test
npm run build
npm run preview
```

The build runs vue-tsc before Vite. Unit tests use jsdom through `vite.config.ts`. Host checks should include its existing type check, tests and production build.

## Mount API

```vue
<script setup lang="ts">
import { MidiMapper, XL3_PROFILE, LAUNCHPAD_PRO_MK3_PROFILE } from './features/midi-mapper/integration'
import type { MidiMapperOptions, ControllerTemplate } from './features/midi-mapper/integration'
import './features/midi-mapper/style.css'
const options: MidiMapperOptions = {
  defaultChannels: { remoteTracks: 15, remoteGlobal: 16 },
  availableDevices: [XL3_PROFILE.id, LAUNCHPAD_PRO_MK3_PROFILE.id],
  savedTemplates: [],
  theme: { className: 'elixir-mapper', accent: '#b5f5c8' },
}
function persist(template: ControllerTemplate) {
  // Store through the host's existing persistence layer, after schema validation.
}
</script>
<template><MidiMapper :options="options" @save-template="persist" /></template>
```

No options are required. Configuration is snapshotted at mount; remount with a new Vue key to replace options. Remounting discards edits, MIDI connections and session-bound restoration state, so preserve backups and finish any hardware transaction first.

| Option | Semantics |
| --- | --- |
| `parameterOverrides` | Full `Parameter` records merged by stable ID into the built-in registry; new IDs are accepted. Partial records are unsupported. Keep Elixir channel groups and preset IDs compatible. Overrides apply to browser, validation, export, monitor, simulation and transfer candidates. They do not alter protocol or upload gates. |
| `defaultChannels` | Both remoteTracks and remoteGlobal integers 1–16; default 15/16. Applies to initial/generated templates, not supplied saved-template contents. |
| `availableDevices` | Nonempty selection of built-in profile IDs. Omitted enables both. Controls selection and rejects JSON/saved templates for disabled devices. Does not verify attached hardware or permit arbitrary new profiles. |
| `savedTemplates` | Editable schema-v1 templates cloned and validated at mount; shown in the template menu. Invalid templates fail mounting. Selection is explicit; first built-in preset is initially loaded. |
| `theme` | Optional className, foreground, background and accent strings. Root foreground/background and primary-button accent are configurable; detailed descendant colors need host CSS scoped under the supplied class. |

Events: `save-template` supplies a detached template when the compact view downloads editable JSON. In the admin view it fires on in-session save (including JSON import/new blank flows). `template-change` supplies a detached current template after edits/replacement; no initial event is promised. Channel edits may temporarily be invalid: validate before persistence. Events never grant hardware consent. There is no exposed imperative upload API or automatic localStorage.

Public exports also include core types, createMappingEngine, schema importTemplate/exportTemplate, built-in profiles and PARAMETER_REGISTRY/Parameter. Pure modules can be imported directly: `elixir/templates.ts` for generateTemplate, `elixir/launchpad-templates.ts` for generateLaunchpadTemplate and either device adapter for its file serializer. Engine flow is `engine.validate(template)`, `engine.resolve(template)`, `engine.exportFile(template, serializer)`. Portable JSON uses schema serialization rather than Vue proxies. Parameter overrides must retain useful remoteTracks/remoteGlobal entries because this interface is an Elixir mapper.

Shared styles are scoped under `.mapper`, with no body reset. `src/standalone.css` resets body margins only in the standalone entry; do not import that file into the host. The compact component uses a viewport-height shell and ResizeObserver to fit the device. If mounting under an existing host navbar, adapt its shell height to the available host space (for example `height: calc(100dvh - var(--host-nav-height))`). Only the sidebar scrolls; no router or store is needed. Import once after host base styles. Existing controller preview styles are component-scoped. Inspect host ancestor selectors and typography for collisions. No router or global store is required; the host chooses navigation, route or conditional mount. Keep the mapper mounted while a hardware restoration is pending.

## Web MIDI and existing host services

Offline editing/export/simulation work without MIDI or permissions. Permission is requested only after Connect MIDI. Hardware requires desktop Chrome/Edge, HTTPS or localhost and user-approved SysEx access (`requestMIDIAccess({ sysex: true })`). Unsupported browsers retain offline features. Embedded frames may need the host's MIDI Permissions Policy configuration; prefer mounting directly in the host page.

The bundled UI owns its Web MIDI session. It does **not** accept a host provider/session prop. The framework-independent provider contract and transport modules can inform a future adapter, but reusing host MIDI infrastructure requires an explicit implementation and tests; it is not a plug-in replacement today. Inspect host listeners and port ownership, prevent concurrent SysEx requests and preserve disconnect/abort cleanup. Close Components during direct device operations. Never request permissions on page load or automatically send on navigation/mount/preset selection.

## GitHub Pages

All runtime work is client-side, with no fetches of research/reference paths. Standalone Vite `base: './'` produces relative assets. In the host, keep its configured Pages repository base (for example `/elixir/`) and asset pipeline; do not overwrite host Vite config wholesale. Verify navigation and reload at the deployed base. The mapper itself has no URL routes; if the host adds a route, use its Pages-compatible hash routing or existing fallback strategy. Serve `dist` via HTTP, not file URLs.

## Verified scope and limits

XL3 file export supports the documented codec subset. Direct upload is restricted to slot1/build1563, absolute explicit-channel CC/off mappings, both-surface validation and stored comparison. MK3 file support is single-pad CC/note/Program Change; direct upload is slot1/build483 with explicit-channel momentary CC/off. Both require fresh backup, review, bound confirmation, identity/original recheck and stored comparison. Keep all existing restrictions. MK3 original restoration is session-bound; keep the downloaded backup before leaving. XL3 arbitrary backup restoration has no standalone UI.

MK3 custom-mode selection uses quick-tap/release Custom, then quick-tap/release leftmost Track Select below the grid. All16 Mute/Solo pads physically passed; original Drums Square was restored byte-for-byte. Other slots/builds, MK3 live note/program/toggle, reconnect persistence and unrelated-slot preservation remain unverified. External LED feedback, fader widgets, surrounding MK3 buttons, voice triggering and relative hardware output are outside verified scope. Broader full-editor visual acceptance and narrow MK3 QA remain pending. Do not turn integration into a claim of full device compatibility.

## Implementation order and acceptance

1. Run standalone tests/build as a baseline. Copy the inventory and merge dependencies. Add a wrapper using the public entry and stylesheet, then add the MIDI Mapper menu entry using the host's navigation conventions.
2. Mount with defaults first. Verify both controllers, five XL3 and three MK3 presets, blank creation, assignment editing/search, channel changes, validation, JSON round trip, .syx download and offline simulator. Offline use must produce zero MIDI permission requests/sends.
3. Implement the generated JSON pipeline and full-registry composition path specified in PARAMETER_JSON.md. Preserve stable semantic IDs. The existing additive parameterOverrides option alone does not remove stale built-in entries. Test a changed parameter number/name in editor, exports and simulator. Test channel defaults, one-device availability, saved-template selection, detached save/change events and invalid configuration. Do not remove preset parameter IDs unintentionally.
4. Add host persistence/theming. Inspect desktop and narrow layouts, keyboard navigation/focus, parameter results, scrollable MIDI monitor and host pages outside the mapper. Confirm mapper styles do not restyle host buttons/headers and host styles do not hide warnings/confirmations.
5. Run copied tests and host regressions/type check/build. Serve production output under the Pages subpath; inspect asset requests, navigation/reload, downloads and offline workflows. Test denied/unsupported MIDI and disconnect cleanup. Confirm unmount disposes listeners/session.
6. Treat hardware acceptance separately: use only verified device/firmware/slot combinations with explicit overwrite consent, preserved original backup, single controlled send and read-back. Reuse recorded fixture regression without claiming a new physical test. Never perform hardware writes merely to validate website integration. Report which browser/physical checks actually ran.

Deliver the host diff, checks and remaining manual acceptance items. Preserve source fixture provenance and the standalone project's protocol safety gates.

## Assignment workflow additions

Include `elixir/assignment-tools.ts` and `components/led-colors.ts` in the copy inventory. ControllerPreview emits `drop-parameter(controlId, parameterId)` using the internal `application/x-elixir-parameter` drag format; MidiMapper validates and commits it. Track-family drops on column1 auto-fill the row when enabled; the click-based Apply across tracks1–7 action offers the same operation without drag. These edits emit the existing template-change event and invalidate prepared transfers through the existing template watcher. Family fill resets that row's overrides to defaults, targets the seven named tracks, and reserves continuous column8 for tracks.master-volume; button/pad column8 is unassigned. Existing preset generation is unchanged. Track/Global scope is a browser filter, not a change to the template's channels or a hardware mode switch. Type filtering and drag validation keep continuous/action constraints intact. Verify named LED options/swatches, search in each scope, single versus bulk drops, atomic incompatible-drop rejection and master-column behavior. `assignment-tools.test.ts` and mapper component tests cover these changes.

## Compact default versus admin view

The default `MidiMapper.vue` contains the compact top bar, channel inputs, .syx download, fitted device preview, and sidebar device switch/Tracks/Global/Config tabs. Parameters use category-only filtering and separate continuous/action groups. Selecting a parameter then clicking a control provides a keyboard/touch alternative to drag. LED colors apply immediately in Config. Parameter labels are fixed; custom assignment labels are not displayed in this view. Config also provides clear assignment, row-fill preference, preset selection and JSON save/open. A MIDI session is constructed lazily when Send to Device is first opened; only explicit Connect MIDI requests permission. Advanced monitoring, receiver/range/channel overrides, validation detail and guarded hardware transfers remain available through `AdminMidiMapper.vue`. Preserve both; do not hide the diagnostic view by deleting it.

The compact initial track layouts remove selected-track assignments from column8 and set XL3 fader8 to master volume. Loading the existing preset catalogs retains their original definitions for regression compatibility. Switching device or loading a preset replaces the workspace; Config's JSON save/open is the persistence path. Host-supplied saved templates remain selectable in Config. Verify the compact workspace at desktop and short-height viewports, sidebar-only scrolling, both device fits, Config color application and JSON round trip. All original admin tests remain; `simple-mapper.test.ts` covers the compact workflow.

## Compact transfer and file controls

The compact topbar includes Send to Device, Import and Download. Download has `.syx` and editable `.json` options. Import accepts mapper schema-v1 JSON only; it validates atomically, restores the controller and channels, and rejects arbitrary `.syx` with an explanatory error. The existing Config JSON controls remain as shortcuts. `destinationSlot` persists the custom-mode choice in JSON; `.syx` serializers still emit Components file exports without addressing hardware slots.

The native dialog keeps its `HardwareConnection` mounted after closing to preserve its session. It uses a compact presentation hiding passive capture/monitor. XL3/MK3 mode counts come from their device profiles (15/8); all modes appear but only1 permits the existing direct-transfer gates. MK3 transfer UI is absent for unsupported slots and the MK3 component also checks destination slot at operation entry. Busy state is forwarded from MK3 through HardwareConnection to prevent dialog closure and port/slot changes during operations. Changing device/unmount still disposes the session; keep the downloaded original backup. Do not introduce automatic connection, preparation, consent or upload in host navigation. Hardware transfer acceptance is inherited from existing tests/evidence, not newly physically repeated for this UI change.
