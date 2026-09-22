# Objective: the voxel editor is how the engine's assets actually get made

**Status:** active

## Outcome
`IRVoxelEditor` is the tool real assets are authored with — creating,
editing, saving, and loading voxel entities end-to-end through the UI is
routine, proven by scripted interaction rather than claimed.

## Done means
- [x] The #766 proof-of-usability lands: five test entities (ant, bird,
  rock, mushroom, tree) authored end-to-end through the editor via
  scripted GUI interaction and saved as `.vxs`.
- [x] The Phase-0 mechanism probes pass: keyboard→command dispatch,
  world→screen mapping accuracy, drag stroke, A/D binding overload
  (`docs/design/editor-authoring-friction.md` §"Phase 0 mechanism probe"
  — run, gate passed).
- [ ] The gui_scale space mismatch is reconciled —
  `GuiInputEvent.screenPx_` and `worldToScreen` agree on one space
  (the known ergonomic gap in `editor-authoring-friction.md`).
- [ ] The editor participates in the verify culture beyond its current
  22 GUI assertions: assertion coverage grows with each authoring
  feature, and editor scenes get reference images or a structural
  oracle.

## Non-goals
Entity-editor epic phases 3–8 (animation timeline, IK/chain solvers,
bind-points, procedural authoring, particles/lights/audio, multi-window
polish — `docs/design/entity-editor-epic.md`) until the human has reviewed
the #766 friction report (`editor-authoring-friction.md` §2g). The 2026-05
phase epics (#606–#613) are retired; a phase revives only as a fresh,
objective-linked decomposition. Adopting dear-imgui — the trixel-rendered
UI is the standing bet.

## Current state
The editor builds clean on macOS/Metal and runs green headless through the
GUI-test harness (22 assertions in `voxel_editor`, including the
load-bearing `PICKS_VOXEL` over the real picking/ray path). Phase 1 static
authoring (#604) shipped; the `.vxs` v1 save format with JSON sidecar has
asset tests. The #766 proof landed (PRs #2472, #2558, #2577, #2593, #3151):
ant (20³), mushroom, rock, bird (two frames) and tree authored end to end
through scripted GUI sessions (`scripts/author-entity.py`), committed under
`assets/voxel/entities/` with sidecars, and played back by
`IRShapeDebug --load-vxs`. The Phase-0 mechanism probes ran and the gate
passed (`editor-authoring-friction.md` §"Phase 0 mechanism probe"). The
friction log §2g records seven findings from the proof; the two it shaped
into defects are approved (#3148 place-below modifier, #3149 widget drag).
Open: the gui_scale space mismatch (row 3) and reference images or a
structural oracle for editor scenes (row 4).

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |
| 2026-09-14 | #766 / PRs #2472, #2558, #2577, #2593, #3151 | five entities authored end to end via scripted GUI sessions; Done-means row 1 verifies |
| 2026-09-14 | `editor-authoring-friction.md` §Phase 0 | mechanism probes run, gate passed; Done-means row 2 verifies |
| 2026-09-19 | #604 close-out; #608/#610/#612/#613 retired; #3148, #3149 approved | phase direction now flows from this objective plus the friction-report review |
