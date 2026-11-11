# Required integration work: generated parameter JSON

Claude Code must implement this in the Elixir website repository during integration. This document defines the contract; the standalone mapper does not yet generate or load JSON.

## One source of truth

Find the Elixir firmware/source definitions that determine the actual MIDI dispatch behavior. Generate a complete remote-parameter JSON from those definitions on **every website build**, before compiling the mapper. Do not regenerate from the old PDF, scrape rendered cheat-sheet text, or export the mapper's current hand-maintained constants and treat that as authoritative. If firmware tables are incomplete, consolidate them with explicitly maintained semantic metadata into one canonical source that also drives documentation. Record where each entry comes from. The cheat sheet and mapper JSON should derive from the same definitions so they cannot drift independently.

The initial scope is the remote-track, remote-global and system registry currently represented by `src/elixir/parameter-registry.ts`. Do not silently add conventional voice CCs, NRPNs or unsupported hardware capabilities just because they exist elsewhere in firmware.

## Build outputs and JSON schema

Suggested output: `<site build output>/data/elixir-midi-parameters.json`, publicly downloadable under the host's GitHub Pages base. Generate a sibling `elixir-midi-parameters.meta.json` with `schemaVersion: 1`, firmware version/revision, source revision and SHA-256 of the exact JSON bytes. Use deterministic row ordering by stable ID; avoid timestamps in the JSON so unchanged inputs produce unchanged bytes. Publish the JSON and manifest with the same site build that consumes them.

Use UTF-8 JSON with actual numeric values, arrays and nested objects. Omit absent optional fields; do not encode them as empty strings or zero. Suggested versioned envelope:

```json
{
  "schemaVersion": 1,
  "firmwareVersion": "<firmware version>",
  "sourceRevision": "<source commit>",
  "parameters": []
}
```

`parameters` contains complete records with these fields:

```text
id, name, shortName, category, scope, track?, channelGroup?, messageType,
alternateMessageTypes, number, minimum, maximum, controlType, interpretation,
buttonBehavior?, encoderBehavior?, feedback?, documentation
```

These are parameter-registry records, **not user controller configurations**. Give the two JSON formats distinct schema validators, filenames and menu labels; do not feed a parameter registry into the existing Import configuration control.

- `id`: stable semantic key, for example `tracks.d1.fx-send`. Keep it unchanged when its CC number or display name changes. Saved templates and row-fill families reference IDs, not MIDI numbers.
- `name`, `shortName`, `category`, `interpretation`: complete display/behavior metadata used by the existing registry.
- `scope`: `remote-tracks`, `remote-global` or `system`.
- `track`: optional `d1`, `d2`, `d3`, `sn`, `cy`, `ch`, `oh` or `selected`. Selected track is not master.
- `channelGroup`: optional `remoteTracks` or `remoteGlobal`. Do not hard-code channel15/16 into the JSON; those are configurable UI defaults. System messages have no channel group.
- `messageType`: `cc`, `note`, `program-change` or `realtime` in the current supported registry.
- `alternateMessageTypes`: Array of strings, including `[]` when empty.
- `number`: the CC/note/program number, or the full real-time status byte; retain the distinction via messageType.
- `minimum`, `maximum`: inclusive value bounds; Program Change uses its documented semantic range, not a fictitious CC value.
- `controlType`: `continuous`, `button`, `selection` or `realtime`. This drives Launchpad filtering and prevents assigning continuous parameters to pads.
- `buttonBehavior`: optional existing behavior enum.
- `encoderBehavior`: optional array matching the registry's supported receiver modes.
- `feedback`: optional object; preserve unverified compatibility status.
- `documentation`: Object with real source locator(s). Extend the current PDF-only TypeScript documentation type to support firmware/source provenance. Never claim a changed value still comes from the old PDF page.

Use separate schema/versioned migration work for new message families or semantics. Do not widen types or protocol gates opportunistically during JSON import.

## How the mapper consumes it

Parse and validate the generated JSON **at build time**, then provide that complete registry to the mapper before mounting. Bundle the validated records so the offline editor has no runtime network dependency. The downloadable JSON is the inspectable/exported copy of exactly those records. Automatically import the generated JSON as part of the site build/bootstrap; users must not have to download and re-import it to get the current map. Add **Download parameter JSON** to the site's existing tools/download menu using its GitHub Pages base. A **Download parameter CSV** link is optional: generate that secondary export from the same validated JSON, never maintain a separate table. Include a format/schema note so users can distinguish parameter data from **Download editable config (.json)** in the mapper. Use the same parser/validator if a runtime loader is later added; runtime loading is not required for this integration.

The existing `options.parameterOverrides` API merges records into a built-in registry. It is useful for additive overrides but is **not sufficient for complete source replacement**, because removed IDs would remain. Add a documented full-registry option/composition path, mutually exclusive with parameterOverrides, and use it for the generated JSON. Thread that registry through presets, engine composition, parameter browser, row-fill, preview labels, simulation, export and transfer validation in both compact/admin views. Audit generators that currently create their own default engine. No path should validate against stale constants while another displays the JSON.

Keep the current registry as a clearly versioned standalone/offline reference fixture if useful; production integration must not silently fall back to it when generation, parsing or validation fails. Persist/reuse the source revision with exported JSON (a versioned, backward-compatible schema change if required), or otherwise show an explicit migration review when a saved template resolves against a different registry revision. Changed CCs should follow the new registry; removed IDs must produce a useful validation error, not an invented replacement or stale mapping. Preserve hardware upload restrictions regardless of registry changes.

## Build failures and acceptance checks

1. Hook JSON generation and validation into the host's normal production build and CI/deployment command, not only a manual script. A clean build must regenerate it and publish JSON + manifest at the correct Pages subpath.
2. Fail the build for missing/unreadable source, missing required fields, malformed JSON, incorrect field types, duplicate/empty IDs, malformed integers, invalid enum/range/channel-group combinations, or presets referencing missing IDs. Detect conflicting MIDI addresses and require explicit allowance for intentional aliases; do not reject documented aliases blindly.
3. Validate all seven members of every row-fill family independently. Do not derive track CCs by adding offsets to the first value unless the canonical source explicitly defines that rule.
4. In a fixture of the canonical source, change one parameter's CC and rebuild. Verify the downloaded JSON, parameter display, resolved assignment, generated .syx bytes and simulator all use the changed number. No hardware write is needed for this test.
5. Verify name/category/controlType changes affect browsing, Launchpad continuous-parameter exclusion and compatibility validation. Verify removed/renamed IDs cause explicit migration/validation failure for presets and saved templates.
6. Round-trip Unicode names, quotes, line breaks, omitted optionals and nested arrays/objects; distinguish numeric zero from missing values and reject numeric strings. Check byte-identical generation for unchanged inputs and manifest hash matching published bytes.
7. Retain existing codec/transport safety tests. Report the firmware revision used and any source/metadata gaps in the integration PR. If the true source cannot be identified, report that blocker rather than claiming the JSON is automatically authoritative.
