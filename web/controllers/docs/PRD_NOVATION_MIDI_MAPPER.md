# PRD: Elixir MIDI Controller Mapper

**Status:** Proposed  
**Project:** Standalone Novation MIDI template generator  
**Development agent:** Codex  
**Integration agent:** Claude Code  
**Target integration:** https://dmajka.github.io/elixir/  
**Reference:** https://dmajka.github.io/elixir/cheatsheet.pdf  
**Primary devices:** Novation Launch Control XL 3 and Launchpad Pro (generation to be confirmed)

---

## 1. Project overview

Build a browser-based MIDI controller configuration application that automatically generates custom MIDI templates for Novation hardware, specifically designed to control the Elixir drum synthesizer.

The application will use Web MIDI and SysEx to configure connected controllers directly, eliminating the need to manually assign parameters using Novation Components.

Development will take place in a **standalone project directory**, separate from the existing Elixir website repository.

The standalone application must be independently runnable, testable, and usable before integration into Elixir.

Once completed, its reusable modules and Vue components will be copied into the Elixir website repository, where Claude Code will integrate them as a new **MIDI Mapper** menu option.

### Desired user workflow

1. Open the MIDI Mapper in a compatible browser.
2. Select a connected Novation device.
3. Choose an Elixir template, such as Mixer, Performance, or Effects.
4. Configure the Elixir remote MIDI channels (defaults: Tracks 15, Global 16).
5. Preview the controller layout and assigned parameters.
6. Customize individual assignments if desired.
7. Select a destination custom-mode slot.
8. Transfer the template directly to the controller using Web MIDI and SysEx.
9. Use the controller with Elixir.

The application should also support exporting compatible `.syx` files for use with Novation Components when direct upload is unavailable.

## 2. Architectural requirements

### 2.1 Standalone development

The initial application must:

- Run independently of the Elixir website.
- Have its own `package.json`, development server, and build process.
- Use Vue 3, Vite, and TypeScript where practical.
- Require no backend server in production.
- Support local development through `localhost`.
- Support automated testing without physical MIDI hardware.
- Support real hardware testing through Web MIDI.
- Avoid dependencies on absolute paths within the Elixir repository.

The existing Elixir website is in a separate directory and must not be modified during standalone development.

### 2.2 Portable integration

The project must be organized so that its functional modules and UI components can be copied into another Vue application.

The final deliverable must include:

- Reusable MIDI mapping engine.
- Novation device profiles and protocol adapters.
- Elixir parameter registry.
- Template generator.
- Web MIDI transport.
- Vue controller configuration components.
- Documentation explaining integration.
- Test fixtures and compatibility information.

The mapper should expose a simple component or composable API so that Claude Code can integrate it without redesigning its internals.

### 2.3 GitHub Pages compatibility

The final feature will run at:

`https://dmajka.github.io/elixir/`

Requirements:

- Client-side operation only.
- No native application dependency.
- No backend API requirement.
- No Electron runtime.
- No external MIDI bridge.
- No dependency on Novation Components after protocol implementation.
- Compatible with GitHub Pages static hosting and the `/elixir/` base path.

Use the browser's Web MIDI API, requesting SysEx access when required.

Initially target desktop Chrome and Edge. Detect unsupported browsers and permissions gracefully.

---

# 3. Elixir MIDI architecture

## 3.1 Authoritative MIDI reference

The primary reference is the Elixir cheat sheet:

https://dmajka.github.io/elixir/cheatsheet.pdf

Page 5 documents MIDI channel configuration and the conventional sound-control CC mappings.

Page 6 documents the dedicated remote controller mappings.

**The mapper should prioritize the page 6 remote mappings for Launch Control XL and Launchpad templates.**

The implementation must preserve the distinction between Elixir's normal MIDI input and its remote controller interface.

## 3.2 MIDI channel configuration

Elixir supports several MIDI channel groups.

| MIDI group | Default channel | Purpose |
|---|---|---|
| Individual tracks | 1–7 | Notes and individual voice control |
| Normal global input | 10 | Patterns, kits, and conventional MIDI input |
| Remote Tracks | 15 | Mixer and per-track remote control |
| Remote Global | 16 | Performance, effects, transport, and global control |

These are defaults, not hardcoded requirements.

The mapper must provide user-selectable MIDI channels for Remote Tracks and Remote Global, each in the range 1–16.

The selected channels must be used in all generated templates, exports, and device uploads.

The UI should explain that these channel settings must match the values configured on Elixir under `CONFIG → REMOTE`.

Warn when the two remote channels are identical or overlap with other configured Elixir MIDI channels. Do not silently change the user's selection.

### MIDI channel settings example

```json
{
  "remoteTracksChannel": 15,
  "remoteGlobalChannel": 16
}
```

Store channel numbers as human-readable values 1–16. Convert to zero-based channel numbers only where required by MIDI encoding.

## 3.3 Remote Tracks map

The following assignments come from page 6 of the cheat sheet.

Track columns:

| Column | Target |
|---|---|
| 1 | D1 |
| 2 | D2 |
| 3 | D3 |
| 4 | SN |
| 5 | CY |
| 6 | CH |
| 7 | OH |
| 8 | Selected track |

All assignments use the user-selected Remote Tracks channel.

| Parameter | CCs, columns 1–8 |
|---|---|
| Volume | 8–15 |
| Pan | 16–23 |
| Filter frequency | 24–31 |
| Resonance | 32–39 |
| Decay | 40–47 |
| Pitch | 48–55 |
| Delay send | 56–63 |
| FX send | 64–71 |
| Reverb send | 72–79 |
| Sample rate | 80–87 |
| LFO amount | 88–95 |
| Density | 96–103 |
| Mute | 104–111 |
| Solo | 112–119 |

For these track-remote controls, CC numbers increase by one across the eight columns.

Example:

- D1 volume: CC 8
- D2 volume: CC 9
- SN volume: CC 11
- CH reverb send: CC 77
- Selected-track volume: CC 15

The eighth column targets the currently selected track rather than a fixed voice.

CH and OH share the underlying hat voice for most sound settings, but have distinct decay behavior. Preserve their separate remote columns.

### Track button behavior

The remote interface supports mute and solo using CC or note messages.

Implement configurable button behavior:

- Momentary press/release.
- Press-to-toggle.
- Toggle-state mode, where supported by the controller.

Ensure the generated messages match Elixir's configured remote button interpretation.

### Encoder behavior

Elixir supports:

- PICKUP
- ABS
- REL

These settings describe how Elixir interprets incoming remote control values. They are not necessarily equivalent to every Novation hardware encoder mode.

The mapper should distinguish device encoder capabilities from Elixir's receiving behavior and provide compatibility guidance.

### MIDI feedback

Elixir can send remote values and button states back over USB.

Investigate whether the target controller supports the required LED or encoder feedback messages and whether this functionality requires additional Novation-specific configuration.

Do not assume that programming a custom mode automatically enables full bidirectional LED feedback.

## 3.4 Remote Global map

All global remote assignments use the user-selected Remote Global channel.

The parameter registry must include the complete page 6 global mapping, including:

**Core performance**

| CC | Parameter |
|---|---|
| 8 | Tempo |
| 9 | Shuffle |
| 10 | Morph |
| 11 | Sample rate |
| 12 | Master filter |
| 13 | Roll rate |
| 14 | Repeat |
| 15 | Tape time |

**FX and reverb**

| CC | Parameter |
|---|---|
| 16 | FX wet |
| 17–20 | FX parameters P1–P4 |
| 21 | Bus dirt |
| 22 | Echo to reverb |
| 23 | Saturate |
| 24 | Reverb level |
| 25 | Reverb size |
| 26 | Reverb damping |
| 27 | Master dirt |

**Density and macros**

| CC | Parameter |
|---|---|
| 28 | Global density |
| 29 | Ghost velocity |
| 30 | Build |
| 31 | Vary |
| 32–35 | Macros 1–4 |

**Additional performance**

| CC | Parameter |
|---|---|
| 36–39 | Drill, Jitter, Cuts, Pump |
| 40–43 | Comb note, Comb feedback, Crush, Reverse length |
| 44–46 | LFO 1–3 depth |
| 47 | Filter resonance |

Also include all additional global mappings documented in the cheat sheet, including:

- Delay parameters, CC 96–103.
- Ring and LFO 4 parameters.
- Transport controls.
- Pattern selection and navigation.
- Track selection.
- Performance buttons.
- Mute/solo-related global functions.
- Other documented remote messages.

Do not limit the registry to the example tables above. Extract the full remote map from the cheat sheet and verify it against firmware definitions when available.

### Program changes and transport

Some Elixir actions use program changes, note messages, or MIDI real-time messages rather than CCs.

Represent MIDI message type explicitly in the parameter registry.

Do not incorrectly encode program changes as CC messages.

Where a controller cannot generate the required message type, identify the incompatibility.

## 3.5 Parameter registry

Build a machine-readable registry.

Example:

```json
{
  "id": "tracks.d1.volume",
  "name": "D1 Volume",
  "category": "Mixer",
  "scope": "remote-tracks",
  "channelGroup": "remoteTracks",
  "messageType": "cc",
  "number": 8,
  "minimum": 0,
  "maximum": 127,
  "controlType": "continuous"
}
```

Use symbolic channel groups rather than storing channel 15 or 16 in every parameter.

The generator must resolve the channel group using the user's settings.

### Registry fields

Include:

- Stable parameter ID
- Display name
- Short display name
- Category
- Scope
- Voice or track
- MIDI message type
- CC/note/program number
- Channel group
- Minimum and maximum values
- Value interpretation
- Button behavior
- Encoder behavior
- Optional feedback metadata
- Documentation reference

Support future expansion beyond the remote mappings to the full set of Elixir voice CCs and NRPNs.

---

# 4. Novation device support

## 4.1 Initial targets

**Launch Control XL 3**

Highest priority. Its mixer-oriented control layout makes it the ideal initial controller.

**Launchpad Pro**

Second priority. Determine the exact hardware generation before implementing its protocol.

Do not assume both devices use identical SysEx formats.

## 4.2 Device profiles

Each device profile must describe:

- Manufacturer and model
- Hardware generation
- MIDI device identifiers
- Available physical controls
- Control groups and coordinates
- Supported MIDI message types
- Encoder behavior
- Button behavior
- LED capabilities
- Custom-mode slot capabilities
- SysEx format
- Upload protocol
- Firmware compatibility

## 4.3 Reusable device adapters

Separate device-specific serialization and communication from the generic mapping engine.

The core application should be able to generate a semantic mapping without knowing the details of Novation's binary format.

---

# 5. Phased implementation plan

## Phase 0 — Standalone project setup

### Objective

Create a clean, independently testable development environment.

### Tasks

1. Initialize a standalone Vue 3, Vite, TypeScript project.
2. Establish unit and integration testing.
3. Create a development interface with navigation.
4. Implement a mock MIDI device provider.
5. Add Web MIDI feature detection.
6. Document the separate Elixir integration target.
7. Confirm that the application builds without access to the Elixir repository.

### Deliverables

- Running standalone web application
- Test configuration
- Initial directory structure
- `research/progress.md`

### Exit criteria

The project starts locally, builds successfully, and runs its tests independently.

Stop and report results.

---

## Phase 1 — Investigate Novation Components Electron application

### Objective

Understand how Novation Components creates, serializes, imports, exports, and uploads custom MIDI modes.

### Tasks

Locate the installed Components application on macOS.

Inspect:

- Electron application resources
- ASAR archives
- JavaScript bundles
- Available source maps
- Device definitions
- Custom-mode schemas
- MIDI transport implementation
- SysEx encoding and decoding
- Slot addressing
- Upload acknowledgement behavior

Use read-only inspection and extract files into a separate research directory.

Search for relevant code using terms such as `sysex`, `customMode`, `serialize`, `deviceId`, `send`, `midi`, `LaunchControl`, and `Launchpad`.

Determine whether the application uses Web MIDI or native MIDI modules.

Document any relevant protocol constants and serialization logic.

Do not copy proprietary source code into the final application.

### Deliverables

- `research/components-architecture.md`
- `research/novation-protocol.md`
- `research/device-capabilities.md`
- `research/evidence-log.md`

### Exit criteria

The agent identifies the most promising method for independently generating compatible custom modes.

If static analysis is insufficient, recommend controlled runtime inspection.

Stop and report results.

---

## Phase 2 — SysEx reverse engineering

### Objective

Develop a verified custom-mode serialization implementation.

### Tasks

Generate baseline custom-mode exports from Components.

Change one setting at a time:

- CC number
- MIDI channel
- Encoder mode
- Button behavior
- LED color
- Note assignment
- Control label
- Destination slot

Compare exported files using binary differential analysis.

Identify:

- Manufacturer and device headers
- Control records
- Parameter offsets
- Message lengths
- Checksums
- Slot identifiers
- Transfer commands

Create a reusable binary comparison utility.

Investigate direct upload separately from file export.

### Deliverables

- `research/sysex-format.md`
- `research/transfer-protocol.md`
- SysEx test fixtures
- Encoder and decoder prototypes
- Binary comparison utility

### Exit criteria

At least one independently generated custom-mode file matches a known-good reference and can be validated through Components.

Do not claim direct upload support until tested.

Stop and report results.

---

## Phase 3 — Elixir MIDI registry

### Objective

Implement the complete documented Elixir remote MIDI map.

### Tasks

1. Extract all Remote Tracks assignments from cheat sheet page 6.
2. Extract all Remote Global assignments.
3. Include CC, note, program change, and other supported message types.
4. Implement configurable Remote Tracks and Remote Global channels.
5. Add metadata for button and encoder behavior.
6. Implement track and category filtering.
7. Add tests for every documented assignment.
8. Document any ambiguous or unsupported entries.

Use the cheat sheet as the initial authoritative reference.

If firmware definitions are available later, compare them with the registry and document discrepancies rather than silently replacing values.

### Deliverables

- `src/elixir/parameter-registry.ts`
- `src/elixir/channel-settings.ts`
- Parameter registry tests
- MIDI map documentation

### Exit criteria

Every documented remote mapping can be looked up by parameter ID and resolved to the correct MIDI message and selected channel.

Stop and report results.

---

## Phase 4 — Generic mapping engine

### Objective

Build the reusable core that assigns semantic parameters to physical controls.

### Tasks

Implement:

- Device profiles
- Physical control definitions
- Parameter assignments
- Template schema
- Mapping validation
- Channel resolution
- Control compatibility checks
- Mapping import/export
- Serialization interfaces

The mapping engine must not depend on Vue or Web MIDI.

### Suggested interfaces

```ts
interface ChannelSettings {
  remoteTracks: number;
  remoteGlobal: number;
}

interface MidiAssignment {
  controlId: string;
  parameterId: string;
  channelGroup?: string;
  channelOverride?: number;
}

interface ControllerTemplate {
  id: string;
  name: string;
  deviceProfileId: string;
  assignments: MidiAssignment[];
}
```

Extend these structures as required for MIDI message types, ranges, LED configuration, and button behavior.

### Exit criteria

The engine can generate and validate a complete controller mapping without connected hardware.

Stop and report results.

---

## Phase 5 — Automatic Elixir templates

### Objective

Generate useful controller configurations from semantic parameter groups.

### Initial templates

**Elixir Mixer**

For Launch Control XL 3:

| Controls | Assignments |
|---|---|
| Faders 1–7 | D1 through OH volume |
| Fader 8 | Selected-track volume |
| Encoder row 1 | Reverb sends, eight track columns |
| Encoder row 2 | Delay sends, eight track columns |
| Encoder row 3 | FX sends, eight track columns |
| Button row 1 | Mute, eight track columns |
| Button row 2 | Solo, eight track columns |

Use the correct CC ranges from the remote map.

**Elixir Performance**

Prioritize density, morph, macros, roll rate, master filter, and performance effects.

**Elixir Effects**

Prioritize delay, reverb, FX bus, feedback, and effect routing.

**Elixir Sound Editor**

Prioritize filter, resonance, pitch, decay, sample rate, and LFO amount across tracks.

**Elixir Sequencer**

Prioritize pattern navigation, track selection, and documented sequencer controls.

### Template generation rules

- Use compatible physical controls.
- Resolve user-selected MIDI channels.
- Respect device limitations.
- Preserve parameter value ranges.
- Support editable assignments.
- Identify unassigned controls.
- Support user-created templates.
- Avoid unsupported assumptions about device control behavior.

### Exit criteria

The standalone application can generate, inspect, and validate complete templates for the Launch Control XL 3.

Stop and report results.

---

## Phase 6 — Standalone user interface

### Objective

Build the complete controller mapper UI before hardware upload.

### Required views

**Device selector**

Choose the target controller and display compatibility information.

**Template browser**

Select Mixer, Performance, Effects, Sound Editor, Sequencer, or a user-defined template.

**Channel configuration**

Choose Remote Tracks and Remote Global MIDI channels, defaulting to 15 and 16.

**Controller preview**

Show a simplified physical layout with assigned parameters.

**Mapping editor**

Click a control to change its assignment, MIDI channel, behavior, or supported properties.

**Parameter browser**

Search and filter Elixir parameters.

**Export interface**

Download the generated custom-mode file.

### UI requirements

- Responsive desktop layout.
- Clear parameter labels.
- Reusable Vue components.
- No dependencies on Elixir-specific routing.
- Avoid proprietary Novation graphical assets.
- Keep device layouts as portable data-driven components where practical.

### Exit criteria

A user can create and export a custom mapping through the standalone application.

Stop and report results.

---

## Phase 7 — Web MIDI and direct hardware upload

### Objective

Configure connected Novation devices directly from the browser.

### Tasks

Implement:

- `navigator.requestMIDIAccess({ sysex: true })`
- MIDI input/output enumeration
- Device identification
- Permission handling
- SysEx message generation
- Upload protocol
- Custom-mode slot selection
- Transfer progress
- Timeouts and errors
- Device disconnection handling

### Hardware safety

Before writing:

1. Validate the device model.
2. Validate the generated SysEx.
3. Identify the destination slot.
4. Warn about overwriting existing configurations.
5. Offer backup/export when supported.
6. Require explicit confirmation.

Do not send unverified or speculative SysEx messages to physical hardware.

### Verification

Distinguish:

- Browser accepted the send operation.
- Device acknowledged the transfer.
- Device stored the template.
- Physical controls emit the expected messages.

These are separate levels of verification.

### Fallback

Retain `.syx` export for use with Components if direct upload is unsupported or unreliable.

### Exit criteria

A real Launch Control XL 3 accepts a generated template and emits the expected MIDI messages.

Stop and report results.

---

## Phase 8 — MIDI monitoring and end-to-end testing

### Objective

Provide tools to validate the generated mapping independently of Elixir.

### MIDI monitor

Implement a browser MIDI monitor showing:

- Input device
- Timestamp
- Message type
- MIDI channel
- CC/note number
- Value
- Resolved Elixir parameter
- Expected versus observed mapping

Allow the user to move a control and immediately see what it transmits.

### Mock device testing

Create a simulated controller that generates MIDI messages from a template.

Use it to verify:

- Channel assignment
- CC numbers
- Note behavior
- Toggle behavior
- Control ranges
- Parameter resolution

### Hardware testing

Test representative controls from every physical control group.

Confirm that:

- Track controls use the configured Remote Tracks channel.
- Global controls use the configured Remote Global channel.
- Changing channels regenerates correct messages.
- Buttons behave as expected.
- Encoders send supported value formats.
- Exports remain compatible with Components.
- Hardware uploads do not corrupt unrelated slots.

### Exit criteria

The standalone application has reproducible automated tests and documented hardware validation.

Stop and report results.

---

## Phase 9 — Launchpad Pro support

### Objective

Extend the mapper to Launchpad Pro hardware.

### Tasks

1. Confirm exact Launchpad Pro generation.
2. Investigate its custom-mode capabilities.
3. Implement its device profile.
4. Reverse-engineer its serialization format where necessary.
5. Implement pad and button assignments.
6. Investigate LED and feedback support.
7. Build a Launchpad layout preview.
8. Create Elixir-specific templates.
9. Validate export and direct upload separately.

### Suggested templates

- Track mute and solo
- Pattern selection
- Performance effects
- Macro controls
- Track triggering

Only expose functions supported by the documented Elixir MIDI protocol.

### Exit criteria

The supported Launchpad Pro generation can use the shared mapping engine with its own device adapter.

Stop and report results.

---

## Phase 10 — Portable integration package

### Objective

Prepare the standalone implementation for incorporation into the existing Elixir website by Claude Code.

**Codex must not directly modify the Elixir website repository.**

### Deliverables

Create an `integration/` directory containing:

- Integration README
- Suggested destination directory structure
- List of required source modules
- List of required Vue components
- Required dependencies
- Required styles and assets
- Public API documentation
- GitHub Pages compatibility notes
- Web MIDI permission requirements
- Build and testing instructions
- Known limitations

### Integration API

Provide a simple, documented entry point for the mapper.

The host application should be able to mount the mapper and optionally supply:

- Parameter registry overrides
- Default MIDI channels
- Device profile availability
- Saved templates
- UI theme configuration

The mapper should not assume a specific router, global store, or application shell.

### Suggested integration target

The existing Elixir website will add a **MIDI Mapper** menu option.

Claude Code will:

1. Copy the portable modules.
2. Add the new navigation entry.
3. Mount the Vue interface.
4. Connect any existing Elixir parameter infrastructure.
5. Reuse existing Web MIDI infrastructure where appropriate.
6. Verify GitHub Pages deployment.
7. Run regression tests.

### Integration documentation

Create `integration/CLAUDE_INTEGRATION.md`.

This document must contain a self-contained implementation plan for Claude Code, including source file locations, dependencies, expected component APIs, and acceptance tests.

### Exit criteria

The standalone mapper builds and tests independently, and a second developer or coding agent can integrate it into Elixir without needing to reverse-engineer the standalone project's architecture.

---

# 6. Suggested project structure

```text
elixir-midi-mapper/
├── PRD.md
├── README.md
├── package.json
├── vite.config.ts
├── src/
│   ├── app/
│   ├── core/
│   │   ├── mapping-engine/
│   │   ├── validation/
│   │   └── templates/
│   ├── elixir/
│   │   ├── parameter-registry.ts
│   │   ├── channel-settings.ts
│   │   └── templates/
│   ├── devices/
│   │   └── novation/
│   │       ├── launch-control-xl3/
│   │       └── launchpad-pro/
│   ├── midi/
│   │   ├── web-midi/
│   │   ├── sysex/
│   │   └── monitor/
│   ├── components/
│   └── tests/
├── research/
│   ├── components-architecture.md
│   ├── novation-protocol.md
│   ├── sysex-format.md
│   ├── evidence-log.md
│   └── progress.md
├── tools/
│   └── sysex-diff/
└── integration/
    ├── README.md
    └── CLAUDE_INTEGRATION.md
```

Adapt the structure to actual implementation needs, but preserve clear boundaries between the mapping engine, device protocols, MIDI transport, and Vue interface.

---

# 7. Important technical considerations

## 7.1 Separate CC namespaces

Elixir's remote map uses different CC assignments from its conventional voice CC mappings.

For example, D1 volume uses CC 8 on the default Remote Tracks channel, while the conventional sound-control mapping uses CC 89 in its separate context.

The registry must preserve both namespaces and prevent accidental mixing.

## 7.2 MIDI feedback

Elixir can transmit control values and button states.

The mapper should investigate whether compatible controller LEDs, rings, or button lights can follow this feedback.

Treat outbound controller configuration and runtime MIDI feedback as separate features.

## 7.3 NRPN support

The initial remote mappings are primarily CC-based.

The architecture should permit NRPN assignments in the future, but only where the target controller supports the necessary message sequences.

## 7.4 SysEx safety

Custom-mode uploads are hardware writes.

Never assume an unknown message is safe simply because it uses the correct Novation manufacturer ID.

## 7.5 Static hosting

No production server-side logic is permitted.

All necessary device definitions, templates, and parameter mappings must be available in the static application.

## 7.6 Licensing

Use Components as a reference for protocol research.

Do not redistribute proprietary application source, artwork, or bundled assets without appropriate permission.

---

# 8. Acceptance criteria

The project is successful when:

- The mapper runs independently in a browser.
- Launch Control XL 3 is supported and hardware-tested.
- Remote Tracks defaults to channel 15.
- Remote Global defaults to channel 16.
- Both remote channels are user-selectable.
- The full documented Elixir remote map is available.
- Users can select semantic templates.
- Users can edit individual assignments.
- Generated templates contain correct MIDI messages.
- Compatible SysEx files can be exported.
- Direct Web MIDI upload works for verified devices.
- A MIDI monitor can validate controller output.
- No backend or Electron runtime is required.
- The code can be copied into the Elixir Vue application.
- Integration instructions are sufficient for Claude Code.
- Existing Elixir functionality remains unaffected after integration.

---

# 9. Codex execution instructions

## Working rules

1. Read this PRD before beginning each phase.
2. Work only within the standalone project directory unless explicitly authorized.
3. Complete phases in order.
4. Maintain `research/progress.md`.
5. Record observed facts separately from hypotheses.
6. Preserve test fixtures and investigation findings.
7. Run tests after each implementation phase.
8. Stop at each phase gate and report results.
9. Do not modify the Elixir website repository.
10. Do not send unverified SysEx messages to hardware.
11. Do not declare hardware validation complete without testing physical hardware.
12. Keep the implementation portable for subsequent Claude Code integration.

## Initial task for Codex

**Execute Phase 0 only.**

Set up the standalone Vue 3 project, testing environment, mock MIDI provider, and research directory.

Locate the installed Novation Components Electron application and record its location and packaging structure.

Review the Elixir cheat sheet, particularly pages 5 and 6, and identify the MIDI parameter data needed for the registry.

Produce the Phase 0 deliverables and recommend the Phase 1 investigation approach.

Do not implement the full mapper or modify the existing Elixir website yet.

---

# 10. Final product vision

The finished feature should allow an Elixir user to open the MIDI Mapper, connect a supported controller, choose a useful configuration, and program the controller in seconds.

Rather than manually assigning dozens of CC numbers in Novation Components, users will work with meaningful Elixir concepts such as mixer levels, effect sends, density, macros, transport, and track controls.

The standalone implementation should also establish a reusable foundation for future MIDI controller support without requiring another complete reverse-engineering or UI development effort.
