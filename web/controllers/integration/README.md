# Portable MIDI Mapper integration

Start with [CLAUDE_INTEGRATION.md](CLAUDE_INTEGRATION.md), the self-contained implementation and acceptance plan. This directory is the handoff package; application source remains in `src/` so the standalone and embedded implementations share one maintained implementation.

Public Vue/TypeScript entry: `src/integration/index.ts`. Required stylesheet: `src/style.css` (explicit import). No runtime images, fonts, proprietary reference files, router, store, server or bridge are required. This package is source intended for a Vue build, not a published npm package or a precompiled standalone library.

The standalone remains the reference implementation. Integration into the separate Elixir website is work for its owner/Claude Code; this phase does not modify that repository.

Default UI: compact `MidiMapper`; preserved diagnostic UI: `AdminMidiMapper`. Both are exported from `src/integration/index.ts`. Standalone admin URL: `?view=admin`, also linked from Config. Use JSON save/open to carry edits between the independent workspaces.

Required during host integration: [generated parameter JSON pipeline](PARAMETER_JSON.md). Every site build must export a firmware-derived JSON and consume the same validated registry, so changed CC values propagate automatically. This pipeline is specified for Claude to implement; it is not yet implemented in this standalone app.
