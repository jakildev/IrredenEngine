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
- [x] The gui_scale space mismatch is reconciled —
  `GuiInputEvent.screenPx_` and `worldToScreen` agree on one space
  (the known ergonomic gap in `editor-authoring-friction.md`).
- [x] The editor participates in the verify culture beyond its current
  22 GUI assertions: assertion coverage grows with each authoring
  feature, and editor scenes get reference images or a structural
  oracle.
- [x] A composite entity — a root plus parts with per-part LOD bands and
  attached components — is authored end to end through the GUI harness from
  a loaded creation module and round-trips through `Prefab.spawn` (#3974).

## Non-goals
Entity-editor epic phases 3, 4, 7, and 8 (animation timeline, IK/chain
solvers, particles/lights/audio, multi-window polish —
`docs/design/entity-editor-epic.md`) remain out of scope. The 2026-05 phase
epics (#606–#613) are retired; a phase revives only as a fresh,
objective-linked decomposition. Adopting dear-imgui — the trixel-rendered UI
is the standing bet.

## Current state
The editor builds clean on macOS/Metal and runs green headless through the
GUI-test harness: the standing shot table holds 22 assertions over 21 shots
(including the load-bearing `PICKS_VOXEL` over the real picking/ray path)
and passes `--gui-layout-check` (PR #4640); `--gui-session` names 30
scripted sessions (`drag_probe` through `composite_proof`, plus
`undo_history` from PR #4689), the fixture module registers eight more
(`creations/editors/voxel_editor/test/module/init.lua`), and `composite_proof` alone evaluates 79
assertions. Every authoring feature since the entity creator landed with its
own session (`voxel_editor/CLAUDE.md` §Validators). Editor scenes have a
committed render-verify reference set
(`creations/editors/voxel_editor/test/references/macos-debug/`, the
`mode_preview_grid` / `mode_preview_revox` pair from
`--gui-session mode_preview_shots`), so Done-means row 4 verifies.
Phase 1 static authoring (#604) shipped; the `.vxs` v1 save format with JSON
sidecar has asset tests. The #766 proof landed (PRs #2472, #2558, #2577,
#2593, #3151): ant (20³), mushroom, rock, bird (two frames) and tree authored
end to end through scripted GUI sessions (`scripts/author-entity.py`),
committed under `assets/voxel/entities/` with sidecars, and played back by
`IRShapeDebug --load-vxs`. The Phase-0 mechanism probes ran and the gate
passed (`editor-authoring-friction.md` §"Phase 0 mechanism probe").

Row 3 is closed: the debug-overlay `worldToScreen` delegates to
`worldPos3DToMouseScreenPx`, the picking chain's inverse, which is the single
owner of the world-to-window-pixel projection and agrees with
`GuiInputEvent.screenPx_`. The uncalled `screenToWorld` inverse, which
projected through the viewport centre alone, is removed.

The entity-creator epic (#3974) closed on 2026-10-13 and its audit list is
drained: remove part (#4181), PARTS list scroll (#4222), linear array offset
(#4221), CENTER-anchored parts (#4245), gizmo drawn in front of its part
(#4348), per-axis rotate rings (#4410), the fill ghost (#4418), module-safe
`Session::Builder` operations with Lua bindings (#4250, #4353, #4376, #4379
RADIUS, #4406 named list scroll), the bake-radius model (#4396, PR #4401),
the panel overlap (#4220, PR #4487), the gizmo-owns-paint-pixels call
resolved by the owner's decision that handles take clicks only in a G
transform mode (#4420, PR #4492; now `voxel_editor/CLAUDE.md` §Picking
contract), and the COPY YAW binding (#4402, PR #4694). Two master breaks
were fixed forward (#4467 by PR #4468, #4642 by PR #4687). Open: #4753
makes the module paths `author-entity.py` writes into
`composite_proof/author.json` portable. Every change since the owner's
2026-09-30 review has been validated by scripted sessions only; no hands-on
pass has landed and the friction doc still ends at Part 3, so the owner's
pass remains the next input and the next decomposition.

The entity creator (#3974) landed its reference proof: the `composite_proof`
session authors nine parts (a stem and a frame from the fixture module's
recipes, a baked blob, and six arrayed copies of the frame) with per-part LOD
bands and the engine's `C_AutoSpin` on the frames. `scripts/author-entity.py
--prefab composite_proof` commits the manifest and parts under
`assets/voxel/entities/composite_proof/`, and `IRShapeDebug --load-prefab`
spawns it for the `composite_proof_z1` / `_z4` / `_z16` references. What
the authoring surfaced is `editor-authoring-friction.md` Part 3.

The owner reviewed the friction report on 2026-09-30. Its picking limits are
routed to #3982 as a fix rather than held as a blocker; #3974 is the
objective-linked decomposition for the entity creator.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |
| 2026-09-14 | #766 / PRs #2472, #2558, #2577, #2593, #3151 | five entities authored end to end via scripted GUI sessions; Done-means row 1 verifies |
| 2026-09-14 | `editor-authoring-friction.md` §Phase 0 | mechanism probes run, gate passed; Done-means row 2 verifies |
| 2026-09-19 | #604 close-out; #608/#610/#612/#613 retired; #3148, #3149 approved | phase direction now flows from this objective plus the friction-report review |
| 2026-09-30 | #3974 (proposal), #3982 | friction-report review done; non-goals narrowed to phases 3, 4, 7, 8; composite-entity Done-means row added |
| 2026-10-13 | #3974 / #3992 | composite proof authored through the harness from the fixture module, committed, and spawned by `Prefab.spawn`; Done-means row 5 verifies |
| 2026-10-15 | sweep | Current state re-verified against master `741d3f0ab`; Done-means row 4 verifies (29 sessions plus the `mode_preview` render-verify set); row 3 confirmed open; entity-creator follow-ups recorded; owner's hands-on pass is the next input |
| 2026-10-16 | #4396 / PR #4401 | the bake slider's held radius is what `bakeSphere` models; `test_bake_radius_guard` fixture |
| 2026-10-17 | #4220 / PR #4487; #4420 / PR #4492; #4467 / PR #4468 | panel overlap fixed; gizmo handles take clicks only in a G transform mode (owner decision); `test_radius` re-pinned |
| 2026-10-20 | PR #4640 | `--gui-layout-check` gates the editor shot table |
| 2026-10-23 | #4402 / PR #4694; PR #4689 | COPY YAW bound to sessions (`test_yaw`); shared undo service and the `undo_history` session (30 sessions, 8 fixture) |
| 2026-10-27 | sweep | re-verified against master `ee85797c0`; audit list drained; row 3 reframed (`worldToScreen` is a dead helper); no Part 4 yet |
| 2026-10-29 | #4767 / PR #4791 | debug-overlay `worldToScreen` delegates to `worldPos3DToMouseScreenPx`; dead `screenToWorld` removed; Done-means row 3 verifies |
