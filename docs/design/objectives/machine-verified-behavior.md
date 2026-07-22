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
- [ ] gui-verify coverage grows beyond its two participants
  (`voxel_editor`, `lua_widgets`): every interactive creation that ships
  hover/click/pick behavior carries assertions.
- [ ] The CI perf gate covers a second surface beyond `IRPerfGrid`, and
  an OpenGL-host baseline exists beside the committed Metal one.
- [ ] The acceptance-evidence loop (author table + `review-acceptance`
  grading, shipped 2026-07-20) is observably in force: recently merged
  planned PRs carry graded `## Acceptance evidence` tables.

## Non-goals
Pixel-reference coverage for every demo (structural/analytic oracles like
`analytic_oracle`'s manifest are the preferred form where pixel refs would
be brittle); gating on hosts the fleet doesn't run.

## Current state
The culture is established and multi-modal: 4 demos with committed
render-verify refs + manifests, a structural-oracle demo with deliberately
zero PNGs, a real CI perf gate (`perf-gate.yml`, >10% mean-frame fails the
PR), 103 test files across 13 domains, and stdlib-only comparators with
their own unit tests. The gaps are host breadth (refs), interaction
breadth (gui-verify), and perf breadth (one demo, one backend).

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
| 2026-07-20 | — | objective seeded |
