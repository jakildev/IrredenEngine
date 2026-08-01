# Plan — #2669: when may the default pivot's depth latch update?

## Plan status: STUB — needs planning before claim

**Part of epic:** #2544 — see
[`.fleet/plans/issue-2544.md`](issue-2544.md) for the epic plan, the Steward
ledger (D1–D7), and the amendment chain (A1–A5).
**Blocked by:** #2547 (merged via PR #2585) — and, more importantly, by the
**pending ruling** below.

## Why this is a stub and not a plan

This issue is a **contract amendment**, not an implementation task. Its own
§Acceptance criteria lead with "a ruling is recorded on this issue selecting
option 1, 2, or 3 (or a fourth)" — and the three options are genuinely different
products (docs-only / re-derive at rotation start / iterate to the fixed point),
not one obvious answer with three spellings. Planning the implementation before
the ruling would be planning the wrong one two times out of three.

Adopted onto epic #2544's `## Children` checklist on 2026-08-01 by the
epic-steward (flow c). `fleet-validate-stack 2544 --state all --check-checklist`
passed with #2669 as the sole `missing-from-checklist` drift item.

## Pickup gate

The question is this iteration's **steward proposal package**:
https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5149886413 —
`fleet:steward-proposal` is applied to umbrella #2544, and its removal is the
re-fire edge. **Do not claim this issue until the ruling lands on #2669.** When
it does, the steward distributes it as an amendment here and to #2548 (P4), and
this stub is replaced by a real plan.

Steward recommendation on record (a recommendation, not a decision): **option 2**
— re-derive at rotation start. Grounds are in the proposal comment and in ledger
D7 / the 2026-08-01 Events entry of the epic plan.

## What any resolution must carry

From #2669 §Acceptance criteria, and worth restating because it is the part most
easily lost:

- `docs/design/camera-yaw-pivot.md` states the chosen latch-update policy
  **explicitly**, including whether a yaw rotation alone re-derives.
- Option 1 additionally requires §"Known deviations" to name **both** (a) the
  pan-settle pop and (b) post-rotate staleness, so a future reader does not
  re-file this.
- Options 2 and 3 additionally require a regression test for the case the current
  policy misses: a **second** rotation from a non-zero yaw pivots about the depth
  of the content actually under the crosshair.
- **The guard has to move the camera between derives.** `scripts/pivot-verify.py`
  holds the camera fixed and sweeps yaw from a single yaw-0 derive, so no current
  block can see either consequence — the same structural blind spot that let the
  world-point-latch defect through two review passes on PR #2585.
  `test/render/camera_pan_pivot_test.cpp` (added by #2585) is the closest
  existing vehicle.

## One correction to carry into pickup

The issue body quotes the gate as `RenderManager::updateDefaultRotationPivotDepth`
at `engine/render/src/render_manager.cpp:358-364`. **That symbol does not exist
on master** — the function is `RenderManager::updateDefaultRotationPivotFocus`
(`render_manager.cpp:310`, declared `render_manager.hpp:136`), and the pan/zoom
key the issue quotes sits inside it further down. The issue was filed against the
pre-amendment naming; `c883c9e1` latched the iso depth but kept the `…Focus`
name. Grep for `updateDefaultRotationPivotFocus`. The quoted *logic* is accurate
— verified on master: the function early-returns on
`m_rotationPivotMode != RotationPivotMode::CAMERA_CENTER || m_hasRotationPivotFocus`,
then on the settle/`depthMatchesView` predicates, then on the pan/zoom equality
the issue names. Only the symbol name drifted.
