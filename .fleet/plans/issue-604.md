# Epic #604 — Entity editor Phase 1: Static voxel authoring

**Type:** `fleet:epic` umbrella ledger (no task plan — the phase's work is
carried by its children; this file exists to hold the ledger).
**Umbrella:** #213 (CLOSED/COMPLETED 2026-07-04)

## Steward ledger

reconciled-through: 2026-09-10 (first steward claim — heal-on-first-claim)
proposal-pending: none

### Children

| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #664 | merged | #691 | — | 2026-09-10 |
| #665 | merged | #679 | — | 2026-09-10 |
| #668 | merged | #694 | — | 2026-09-10 |
| #761 | merged | #785 | — | 2026-09-10 |
| #762 | merged | #794 | — | 2026-09-10 |
| #763 | merged | — (see E4) | — | 2026-09-10 |
| #764 | merged | #797 | — | 2026-09-10 |
| #765 | merged | #933 | — | 2026-09-10 |
| #766 | open | #3151 | plan | 2026-09-10 |

Position: **8 of 9 closed.** #766 is the sole open child.

### Decisions

- D1 (2026-09-10): the epic's membership is the nine issues above. All nine
  declare it as `- Parent epic: #604`, a spelling no discovery path parses
  (see F1) — source: each child's body, read directly; the 2026-05-16
  dependency-order comment on #604 names #761–#766, and #664/#665/#668
  declare #604 as parent from the asset track.
- D2 (2026-09-10): #763's closure is **real despite carrying no closing PR
  reference**. Its cited PR #791 is CLOSED-not-merged, but the capability
  ships on master: `creations/editors/voxel_editor/editor_layer_manager.hpp`
  plus the LAYERS panel at `main.cpp:3471-3489`. Recorded as capability
  evidence, not a PR link — source: `git grep` against `origin/master`.

### Findings

- **F1 (2026-09-10) — a third membership spelling, and it is this whole
  track's spelling.** Every one of the nine children declares membership as
  `- Parent epic: #604`, which contains neither the canonical
  `**Part of epic:** #N` field nor the `Part of:` + `epic #N` variant that
  #3141 already records. `fleet-validate-stack 604 --state all` returns
  **`0 child issue(s)`** — with the #1881 control returning 4 on the same
  invocation, so the zero is measured, not an empty tree. Consequence: this
  epic showed a `[0/0]` checklist for 122 days and fired no trigger of any
  kind. Appended to #3141 rather than filed fresh.

### Events

- 2026-05-10: epic filed.
- 2026-05-16: dependency-ordered implementation tickets filed (#761–#766).
- 2026-05-13 → 2026-05-19: #664, #665, #668, #761, #762, #763, #764, #765
  all merged.
- 2026-09-09 (backlog sweep): #766 **reopened** — PR #2577's `Closes #766`
  closed it early; PR #2593, merged nine minutes later, states the bird and
  tree slices remain.
- 2026-09-10: first steward claim. `## Children` checklist healed from
  empty to the nine members above (eight ticked). No child moved.
- 2026-09-10: **PR #3151** ("F-1.6 PR-4 — BIRD + TREE, closing the
  five-entity proof") is open, `fleet:approved`, `MERGEABLE`, base `master`.
  It is the residual #766 names. When it merges the epic is 9/9 and
  close-out becomes reachable — the acceptance sweep is then the only
  remaining gate.
