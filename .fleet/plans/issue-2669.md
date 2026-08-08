# Plan — #2669: when may the default pivot's depth latch update?

## Plan status: PLANNED — the ruling landed 2026-08-05 (option 2 ratified); the plan of record is **A2** at the bottom of this file

**Part of epic:** #2544 — see
[`.fleet/plans/issue-2544.md`](issue-2544.md) for the epic plan, the Steward
ledger (D1–D7), and the amendment chain (A1–A5).
**Blocked by:** #2547 (merged via PR #2585) — and, more importantly, by the
**pending ruling** below.

## Why this is a stub and not a plan

> **Superseded by A2 (2026-08-05 ruling).** This section explains why the file
> *was* a stub; the ruling it was waiting for has landed and the plan of record is
> now **A2** at the bottom. Kept for the audit trail — plans are append-only.

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

> **LIFTED — superseded by A2.** The ruling landed 2026-08-05 and
> `fleet:steward-proposal` was removed from #2544; the do-not-claim gate below no
> longer applies. Read **A2** for the ratified decision and the acceptance
> criteria. (The one gate that *does* still apply is unrelated to design: this
> issue carries no labels, so it is not queued.)

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

### A2 — 2026-08-08 — trigger: proposal answered (architect ruling on umbrella #2544, 2026-08-05)

- **Decision: option 2 is ratified — the default pivot's depth latch re-derives
  at rotation start**, i.e. on the first frame yaw changes, whose previous frame
  was still and therefore holds a valid depth attachment. This is *in addition
  to* the pan/zoom-scoped derives #2585 shipped, not a replacement for them.
  Three conditions ride the ruling and are binding:
  1. **Gesture-start only.** No per-frame derives during continuous rotation.
     The single flush at the first yaw-delta frame is the accepted cost. If that
     stutter later measures objectionable, that is a **new** issue with its own
     measurement — not a re-litigation of this ruling.
  2. **Contract (A) is amended**, from "pan/zoom-scoped latch" to "pan/zoom-scoped
     latch + rotation-start re-derive". `docs/design/camera-yaw-pivot.md` states
     the amended contract and names both consequences it resolves: (a) pan-settle
     pop, now masked inside the rotation that immediately follows it, and (b)
     post-rotate staleness, now gone because every rotation pivots about a fresh
     depth. Note the shape change from the stub's §"What any resolution must
     carry": option 1's §"Known deviations" requirement does **not** apply — under
     option 2 (a) and (b) are resolved, not documented as deviations.
  3. **Verification must move the camera between derives.** Explicitly accepted as
     a close-out condition of epic #2544: no current `pivot-verify.py` block can
     observe either failure mode, so a green existing sweep is **not evidence on
     this question**. A block must pan (changing the height under the crosshair),
     then rotate, and assert the pivot depth was re-derived at rotation start.
  Grounds of record are the steward's two, adopted verbatim by the architect:
  the cost asymmetry against option 3 (`depth_probe.hpp` documents the full-flush
  cost as the reason the probe stays single-pixel and debug-gated, so an N-flush
  fixed-point loop would make a per-derive production path out of a primitive
  documented as too expensive for one), and pivot-source consistency with D4 —
  Phase 4's cursor latch already re-acquires depth at gesture start
  (`system_camera_mouse_rotate.hpp:67-70`, re-verified in A1 item 2), so option 2
  is that same policy applied to the default pivot, and option 1 would leave the
  two pivot sources with different staleness behaviour, precisely the fork D4 was
  chosen to avoid.
- **Supersedes:**
  - `## Plan status` — **STUB → PLANNED**. The stub's entire reason to be
    (§"Why this is a stub and not a plan": "planning the implementation before the
    ruling would be planning the wrong one two times out of three") is discharged;
    the ruling picked one of the three products. The status line at the top of this
    file is updated to point here, and this bullet is the record of that flip.
  - §"Pickup gate" **in full** — the do-not-claim gate is **lifted**. The
    `fleet:steward-proposal` label was removed from #2544 on 2026-08-05T01:37:24Z
    (the re-fire edge), and this amendment is the distribution the gate was waiting
    for. The "steward recommendation on record (not a decision)" framing is also
    superseded: option 2 is now a ratified decision, epic ledger **D11**.
  - A1's "the ruling is **still pending** … the do-not-claim gate stands unchanged".
  - Options **1** and **3** are closed. Option 3 (iterate to the fixed point) is
    rejected on the documented cost of the primitive it would call, and A1 item 2's
    finding sharpens why: the cursor path's background-click fallback is
    `IRRender::getDefaultRotationPivotFocus()` (`cursor_pivot.hpp:66`), so an
    N-readback loop inside the derive would be paid on every background click of a
    cursor-pivot drag as well.
- **Acceptance criteria** (superseding the stub's "a ruling is recorded … selecting
  option 1, 2, or 3"), which is now the criteria list a worker implements against:
  1. `RenderManager::updateDefaultRotationPivotFocus` re-derives on the first frame
     of a yaw change — the rotation-start edge — in addition to the existing
     pan/zoom key. **Symbol note carried forward from §"One correction to carry into
     pickup": the function is `updateDefaultRotationPivotFocus`
     (`render_manager.cpp:310`, declared `render_manager.hpp:136`), not the
     `…Depth` name the issue body quotes.** That correction was verified on master
     and #2659 touched no `render_manager` surface, so it still holds — re-verify
     against current master before editing.
  2. **No per-frame derive during a continuous rotation** — condition 1 above. The
     implementation must show the derive fires once per rotation gesture, not once
     per frame of it.
  3. `docs/design/camera-yaw-pivot.md` states the amended contract (A) explicitly,
     including that a yaw rotation alone **does** now re-derive, and names (a) and
     (b) as resolved by it.
  4. A regression guard that **moves the camera between derives**: pan (changing
     the height under the crosshair), then rotate, then assert the depth was
     re-derived at rotation start. The stub's second-rotation case
     ("a second rotation from a non-zero yaw pivots about the depth of the content
     actually under the crosshair") is the natural companion assertion.
     `test/render/camera_pan_pivot_test.cpp` (added by #2585) remains the closest
     existing vehicle; a `pivot-verify.py` block is the alternative, but note that
     no existing block — including P4's `cursor-latch`, re-checked in A1 — moves
     the camera between derives, so this is a **new** block either way.
  5. Epic-scope: this criterion is also epic #2544's Finding **F3** and a stated
     close-out condition of the ruling. Close-out cites the guard, not a green
     sweep of the pre-existing blocks.
- **By:** epic-steward — source: architect ruling
  https://github.com/jakildev/IrredenEngine/issues/2544#issuecomment-5186529073
  ("## Architect ruling — depth-latch update policy", 2026-08-05T01:37:22Z),
  answering the 2026-08-01 STEWARD PROPOSAL
  (issuecomment-5149886413); `fleet:steward-proposal` removed from #2544 at
  2026-08-05T01:37:24Z (verified live in the issue timeline). Epic-side record:
  `.fleet/plans/issue-2544.md` **D11** and **A7**.
