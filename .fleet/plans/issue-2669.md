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

## Amendments

<!-- append-only; newest wins where it contradicts older plan text. Format per
     docs/agents/epic-steward-protocol.md §"Plan amendments (append-only)". -->

### A1 — 2026-08-04 — trigger: PR #2659 merged (P4 / #2548)

- **Decision:** Phase 4 has **shipped**, which changes three things for this
  issue. It does **not** change the pending question, and this stub stays a stub.
  1. **The ruling's distribution surface is now half code.** The stub said the
     steward would distribute the answer "as an amendment here and to #2548
     (P4)". #2548 is closed (merged 2026-08-04T17:56:47Z, master `e640a5b1`), so
     P4 is no longer a plan to amend — it is
     `engine/prefabs/irreden/render/cursor_pivot.hpp` and
     `…/systems/system_camera_mouse_rotate.hpp` on master. An option-2 or
     option-3 ruling must be checked against **that code**, not against §Phase 4
     text, and any behavioural change it implies for the cursor path is a new
     child, not an amendment to a closed one.
  2. **Option 2 is confirmed free for the cursor path, by construction rather
     than by argument.** `System<CAMERA_MOUSE_ROTATE>::endTick` calls
     `CursorPivot::resolveFocusWorld` on the mouse-DOWN **edge**
     (`system_camera_mouse_rotate.hpp:67-70`) and `clearRotationPivotFocus()` on
     release (`:93`) — i.e. re-acquire at gesture start, which is exactly what
     option 2 prescribes for the default pivot. Option 3 remains the expensive
     one on the same primitive: the cursor path's background-click fallback is
     `IRRender::getDefaultRotationPivotFocus()` (`cursor_pivot.hpp:66`), so an
     N-readback fixed-point loop inside the derive would be paid on every
     background click of a cursor-pivot drag, not just on the default pivot's own
     path. A5 item 3 predicted this; the shipped code confirms it.
  3. **"World point vs latched depth" is decided by latch LIFETIME, not by pivot
     source** (epic ledger **D9**). The cursor pivot stores a world point and is
     correct doing so *because* the latch spans one gesture and no pan can begin
     inside it (`System<CAMERA_MOUSE_PAN>` starts only on the `middlePressed`
     edge with Ctrl not held, `system_camera_mouse_pan.hpp:29-32`). The default
     pivot's latch, by contrast, spans arbitrary camera motion — which is the
     whole of this issue's question. A ruling that lengthens or shortens that
     lifetime therefore also decides whether an iso-depth latch is still required
     there; do not read the cursor path's world-point latch as precedent for
     relaxing it.
- **Supersedes:** §"Pickup gate"'s "the steward distributes it as an amendment
  here and to #2548 (P4)" only — the #2548 half is now code (item 1 above). The
  ruling is **still pending** (`fleet:steward-proposal` re-verified live on
  umbrella #2544 on 2026-08-04), the do-not-claim gate stands unchanged, and the
  steward recommendation on record is still option 2.
- **Acceptance criteria:** unchanged, and the one most at risk is **reconfirmed**:
  §"What any resolution must carry" requires a guard that **moves the camera
  between derives**. P4 added a `cursor-latch` block to `scripts/pivot-verify.py`,
  and it does *not* close that gap — it parks a synthetic cursor on one pixel,
  latches once, and holds the latch across a 9-yaw sweep with the camera fixed
  (proving the latch holds, which is the opposite property).
  `test/render/camera_pan_pivot_test.cpp` remains the closest existing vehicle.
  §"One correction to carry into pickup" (the symbol is
  `updateDefaultRotationPivotFocus`, not `…Depth`) also survives — #2659 touched
  no `render_manager` surface.
- **By:** epic-steward — source: PR #2659 §Summary and §Verification; verified on
  `origin/master` at the four cited call sites; epic #2544 ledger D9, A5 item 3,
  and Finding F3.
