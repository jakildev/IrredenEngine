# Objective: behavior changes are provable by harness on every supported host

**Status:** active

## Outcome
No reviewer — human or agent — ever takes "it looks right" on faith. Every
behavior-bearing change is demonstrable by a committed harness (render
refs, GUI assertions, perf gate, acceptance evidence), on the hosts the
engine actually ships from.

## Done means
- [ ] render-verify reference sets exist for `linux-debug` and
  `windows-debug` on the covered demos — today refs are essentially
  macOS-only (shape_debug 33 / lighting 51 / fog_demo 13 / canvas_stress
  14 PNGs, vs a single 6-PNG linux set for canvas_stress and zero
  windows refs).
- [x] gui-verify coverage grows beyond its two participants
  (`voxel_editor`, `lua_widgets`): every interactive creation that ships
  hover/click/pick behavior carries assertions.
- [ ] The CI perf gate covers `IRLuaPerfGrid` beside `IRPerfGrid`; an
  OpenGL-host baseline still needs to exist beside the committed Metal one.
- [x] The acceptance-evidence loop (author table + `review-acceptance`
  grading, shipped 2026-07-20) is observably in force: recently merged
  planned PRs carry graded `## Acceptance evidence` tables.

## Non-goals
Pixel-reference coverage for every demo (structural/analytic oracles like
`analytic_oracle`'s manifest are the preferred form where pixel refs would
be brittle); gating on hosts the fleet doesn't run.

## Current state
Twelve creations carry render-verify manifests
(`creations/{demos,editors,launcher}/*/test/references/manifest.json`;
`analytic_oracle` is structural-only with zero PNGs), and since PR #4706 each
manifest declares the presets it gates through `sweep_backends`, so
`render-verify --all` can pass on a GL host. Reference breadth by preset:
`windows-debug` sets exist for shape_debug (33), fog_demo (50), canvas_stress
(8), lighting (5) and perf_grid (7) (PRs #3065, #3834); `linux-debug` has only
the stale 6-PNG canvas_stress set (#1969 open, needs a GL host; the perf_grid
bless #3544 open), so row 1 stays open on its Linux half. gui-verify has
seven participants — voxel_editor, lua_widgets, shape_debug, canvas_stress,
ui_widgets, font_maker and the launcher — which is every creation that
registers widgets, so row 2 verifies (the `gui-verify` skill's participant
table lists all seven since PR #4771). The perf gate (`perf-gate.yml`; a >10%
mean-frame regression fails the PR) measures `IRPerfGrid` and, since PR #4776
(#4766), `IRLuaPerfGrid` on GPU-less hosted Linux runners, with six
`baseline_latest/linux-x86_64-*` slugs on the `perf-baseline` branch; no GPU
OpenGL or Windows baseline exists (#3814 and #4155 are parked on a quieted
Windows host), so row 3 stays open on its baseline half. The
acceptance-evidence loop is in force: ten of the last fifteen merged PRs carry
graded `## Acceptance evidence` tables and every one without a table has no
`Closes` line, so row 4 verifies. `test/` holds 355 tracked files across 19
domains; the harness comparators stay stdlib-only with their own unit tests.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |
| 2026-07-31 | PR #2622 | shape_debug joins gui-verify (help overlay, perf HUD, settings menu) |
| 2026-09-06 | PR #3065 | `windows-debug` reference set for shape_debug |
| 2026-09-26 | PR #3834 | `windows-debug` sets for canvas_stress, lighting, fog_demo, perf_grid |
| 2026-10-01 | PR #3993 | canvas_stress `hover_canary` gui-session |
| 2026-10-18 | PRs #4537, #4579 | ui_widgets and font_maker gui assertions |
| 2026-10-19 | #4531 / PR #4600 | launcher gui assertions |
| 2026-10-23 | #4660 / PR #4706 | per-backend `sweep_backends` manifest scope; `render-verify --all` can pass on a GL host |
| 2026-10-27 | sweep; #4766 | re-verified against master `ee85797c0`; rows 2 and 4 ticked; #4766 filed for the perf gate's second surface (row 3) |
| 2026-10-27 | #4769 / PR #4771 | the `gui-verify` skill's participant table lists all seven GuiTest participants (sweep finding) |
| 2026-10-28 | #4766 | perf-gate second-surface half: `IRLuaPerfGrid` added beside `IRPerfGrid`; OpenGL-host baseline remains #3814 |
