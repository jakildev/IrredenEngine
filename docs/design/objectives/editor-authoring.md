# Objective: the voxel editor is how the engine's assets actually get made

**Status:** active

## Outcome
`IRVoxelEditor` is the tool real assets are authored with — creating,
editing, saving, and loading voxel entities end-to-end through the UI is
routine, proven by scripted interaction rather than claimed.

## Done means
- [ ] The #766 proof-of-usability lands: five test entities (ant, bird,
  rock, mushroom, tree) authored end-to-end through the editor via
  scripted GUI interaction and saved as `.vxs`.
- [ ] The Phase-0 mechanism probes pass: keyboard→command dispatch,
  world→screen mapping accuracy, drag stroke, A/D binding overload
  (`docs/design/editor-authoring-friction.md` — probes defined, not yet
  run).
- [ ] The gui_scale space mismatch is reconciled —
  `GuiInputEvent.screenPx_` and `worldToScreen` agree on one space
  (the known ergonomic gap in `editor-authoring-friction.md`).
- [ ] The editor participates in the verify culture beyond its current
  5 GUI assertions: assertion coverage grows with each authoring
  feature, and editor scenes get reference images or a structural
  oracle.

## Non-goals
Entity-editor epic phases 3–8 (animation timeline, IK/chain solvers,
bind-points, procedural authoring, particles/lights/audio, multi-window
polish — `docs/design/entity-editor-epic.md`) until static authoring
proves out through #766. Adopting dear-imgui — the trixel-rendered UI is
the standing bet.

## Current state
The editor builds clean on macOS/Metal and runs green headless through
the GUI-test harness (5/5 assertions including the load-bearing
`PICKS_VOXEL` over the real picking/ray path). Phase 1 static authoring
(#604) shipped; the `.vxs` v1 save format with JSON sidecar has asset
tests. The honest gap the friction log records: nobody has yet authored
a real asset all the way through, and the #766 probes that de-risk that
are groundwork-only.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |
