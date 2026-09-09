# Plan: render: detached re-voxelize world-shadow CAST produces no Metal output

**Issue:** #2091. **Part of epic:** #1717 (cast facet; #2080 is the receive facet).
**Model:** opus.

## Where the plan lives

The **canonical plan is the `## Plan` comment on #2091**
([2026-06-27T20:30:42Z](https://github.com/jakildev/IrredenEngine/issues/2091#issuecomment-3009244268)),
plan-reviewed the same day and approach-signed-off 2026-07-04. This file was
materialized by the epic-steward on 2026-09-09 because epic #1717's ledger
already cited `issue-2091.md` in its Plan column while no such file existed on
`master` — a ledger claim with nothing behind it (the #2571 shape). It is a
**pointer plus the amendment log**, not a second copy of the plan: read the
comment first, then every amendment below, newest wins where they conflict.

> Note for `fleet-queue-ingest`: this file satisfies `_plan_exists()`, which is a
> content-blind file-existence probe. That is correct here — #2091 **is** planned
> and signed off. It is not a planning stub.

## Amendments

### A1 — 2026-09-09 — trigger: epic #1717 re-validation (PR #3020 merged `Refs #2091`; #1640 closed by PR #2307)

- **Decision:** Phase 0 is **complete and its fork is settled at (a) — #2091 is a
  Metal-only backend gap.** Phase 1 is the whole remaining fix, it is
  **macOS/Metal-bound**, and it is **no longer gated on #1640**. Phase 1's
  starting point is *not* the one the plan and the 2026-07-08 architect note
  name; the live candidate is the #2488 own-canvas
  `resolveImageAtomicScratch` gap.
- **Supersedes:** three premises in the `## Plan` comment.
  1. §"Blocked by" and §"Approach" Phase 1 — *"gated on #1640 merge … consume
     #1640's landed foreign-canvas R32I read mechanism verbatim — no new design
     here"*. **#1640 is CLOSED/COMPLETED 2026-07-09T02:29:51Z**, closed by merged
     PR **#2307** ("Metal headless GPU vehicle-A harness + R32I cross-encoder
     regression guard (closes #1640)") — a harness and a *regression guard*, not
     a new read mechanism. So there is **no mechanism to consume**: the gate is
     discharged and Phase 1's prescribed approach has no referent. A worker
     following the plan as written goes looking for a landed #1640 mechanism and
     finds none.
  2. §"Verified current state" — *"#1640 is **unresolved** … currently
     `human:review-plan` (a fresh plan under human review, no impl PR)"*. True on
     2026-06-27, false since 2026-07-09.
  3. §"Approach" Phase 0 — now **executed**, on `pool-4` (native Windows /
     MSYS2, `windows-debug`, OpenGL), recorded in PR **#3020** (merged
     2026-08-22, docs-only, deliberately `Refs` not `Closes`). GL renders the
     world-placed detached cast: floor-ROI `shadow_px` **1189** (single
     component) against **0** for floor-alone and **0** for
     `--screen-lock-detached`, with a GRID positive control at 4574.
- **Acceptance criteria:** the four criteria stand as written; two are now
  discharged and two are open, and the split is what gates epic #1717's
  close-out.
  - **GL cast — MET.** Established by Phase 0 (PR #3020), which is the
    discharge route the criterion itself names.
  - **No regression to GRID-cube floor casts — MET.** Same run's positive
    control.
  - **Metal cast — OPEN.** The whole residual.
  - **Render-verify reference shot passing on both backends — OPEN, and it
    cannot pass while the Metal half is out.** Verified against the tree
    2026-09-09: `creations/demos/canvas_stress/test/references/` carries
    `macos-debug/` (8 refs) and `linux-debug/` (6) and **no `windows-debug/`
    set**, so the GL host that measured Phase 0 cannot bless it either.
- **Do not re-derive (two dead ends, both already paid for):**
  1. **Backend read visibility.** Cleared by #2307; the per-axis resolve already
     performs a second in-tick foreign-canvas R32I read every non-cardinal frame
     on Metal in production.
  2. **The FrameData-staleness hypothesis** — the 2026-07-08 architect note's
     designated Phase-1 starting point — is **REFUTED**, code-grounded, on
     2026-08-22. The per-caster loop
     (`system_bake_sun_shadow_map.hpp:443-461`) patches only the 16-byte
     `detachedWorldReceive_` lift via `subData` and re-binds the UBO after each
     patch; `MetalBufferImpl::subData`
     (`engine/render/src/metal/metal_buffer.cpp:52-100`) orphans the buffer when
     it is already encoded, so a dispatch encoded against version *N* keeps
     reading *N*. The mechanism cannot fire as written.
- **Live candidate for Phase 1 (candidate, not a verified root cause):** the
  caster canvas's distances are never materialized out of the Metal
  image-atomic scratch. `resolveImageAtomicScratch` has exactly one call site
  outside the backend (`system_voxel_to_trixel.hpp:1949` — the 2026-08-22 comment
  cites `:1917`; re-resolved against this PR's base), and it resolves the
  canvas *currently being ticked*; the world-placed scatter reads a **different**
  canvas — the detached caster's own model-frame distance texture
  (`c_resolve_world_placed_depth.metal:19`). This is the *own-canvas* half of the
  #2488 rule, narrower than the #1640 foreign-read gap this thread circled, and
  it post-dates every prior investigation here. First probe: call
  `IRRender::device()->resolveImageAtomicScratch(caster.textures_->getTextureDistances())`
  per caster immediately before the scatter loop at
  `system_bake_sun_shadow_map.hpp:443`, then re-run the #2090 side-by-side. It is
  a no-op on GL by construction, so the measured-green GL side stays identical.
  If the cast does not appear, check next whether the caster canvas's stage-2
  winner tap runs at all for a detached re-voxelize pool.
- **Routing (the reason this issue is parked, not queued):** `fleet:needs-gl-host`
  was correctly **removed** 2026-08-21 by the pane that finished the GL-gated
  slice — leaving it would tell macOS panes to skip the one host that can now
  finish the work. There is **no inverse label**: verified against
  `gh label list` on 2026-09-09, the vocabulary has `fleet:needs-gl-host` and the
  PR-side smoke labels (`fleet:needs-macos-smoke`, `fleet:verified-macos`) but
  **no issue-side macOS-host claim gate**. #2820, which owned the host-gate model
  and carried this exact datapoint, closed COMPLETED 2026-09-06 via PR #3000 —
  which *narrowed* `fleet:needs-gl-host` with a backend-symmetric discriminator
  and did **not** add the inverse. So the residual still cannot be positively
  routed, and a second GL pane already burned an iteration discovering that
  (2026-08-22). #2091 is therefore parked `fleet:needs-human` (with
  `human:approved` retained), which is the correct park and not a stall to
  "fix" by re-queueing it blind.
- **By:** epic-steward — source: the `## Plan` comment §Approach; #2091 comments
  [2026-08-21T23:32:03Z](https://github.com/jakildev/IrredenEngine/issues/2091#issuecomment-5462857836)
  (Phase 0 complete, fork (a), `fleet:needs-gl-host` removal rationale) and
  [2026-08-22T23:07:34Z](https://github.com/jakildev/IrredenEngine/issues/2091#issuecomment-5467440206)
  (FrameData refutation, the #2488 candidate, the park); #1640 state + PR #2307
  title; PR #3020 file list and merge state; `git ls-tree origin/master
  creations/demos/canvas_stress/test/references/`; `gh label list --repo
  jakildev/IrredenEngine`.
