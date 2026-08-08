# Plan — issue #2321 (S3: sun-shadow softness — PCF width + zoom-aware texel budget)

**Part of epic:** #2314 (render: lighting/shadow domain culling correctness + validation infrastructure)
**Model:** opus
**Blocked by:** #2385

This per-child plan file was created by epic-steward at flow-a distribution
(2026-07-15) when the S3 design proposal was answered. Before this, #2321's
plan was the epic plan §S3 (`.fleet/plans/issue-2314.md`) plus the issue body.
Those remain the base; this file adds the append-only amendment recording the
architect ruling. Read top-to-bottom — the newest amendment wins where it
contradicts the base framing.

## Base (as originally scoped, epic §S3 + issue body)

S3 improves sun-shadow silhouette softness on the receive/extent side, on the
post-S2 (#2320) baseline. Two levers were framed:
- **Lever (a):** zoom-aware near-cascade extent fit ("fit the near cascade to
  the visible frustum" → finer sun texels at low zoom).
- **Lever (b):** 3×3 soft PCF receive kernel (wider receive tap).

Original acceptance gate: silhouette-jaggedness metric improves **≥2×** at
zoom 1–2 (`render-shadow-metric.py` components / largest_frac). Keep S1's
splat/bias math untouched (D4). Verify cardinal + ~30° + 45° yaw on both
backends (D3), using the V3 light-verify harness (#2317).

## Amendments

### A1 — 2026-07-15 — trigger: STEWARD PROPOSAL 2026-07-14 answered by opus-architect

- **Decision:**
  - **Sequencing (Q1 → a):** #2321 is now **Blocked by: #2385**. #2385
    (genuine-cast under-coverage re-fix on the decontaminated baseline) lands
    **first** — it is now the epic's critical path. The zoom-4 residual the
    worker measured is a hexagonal honeycomb of unshadowed floor holes = the
    #2385/#1717 bake under-coverage signature (missing sun-map writes), which
    S3's receive/extent levers cannot touch. S3's render-shadow-metric gate is
    therefore un-measurable (contaminated) until #2385 clears the honeycomb.
  - **Lever (a) DROPPED — measured-refuted.** Zoom-aware near-cascade extent
    fit is a structural no-op on Metal (two independent reasons, both
    measured/geometric): (i) sizing the near slab to the visible viewport span
    clamps to today's full 204.8-voxel slab at every tested zoom (visible
    iso-depth span 341–1364 ≫ 204.8) → **byte-identical** shadow output
    (components 88/71, largest_frac 0.88/0.48 at z2/z4 unchanged); (ii) the
    on-screen receivers (central-cluster floor at iso-depth ≈ 0) are served by
    the FAR cascade (`cascadeSplitDepth = -51.2`), which must keep its full
    slab as the receiver's covering-cascade fallback and cannot be tightened.
    Sun texels already track zoom sub-linearly (near 0.45→0.15 across zoom
    0.55→4). Do NOT re-attempt the near-cascade extent fit.

    | zoom | near texel0 | far texel1 | visible iso-depth span |
    |---|---|---|---|
    | 0.55 | 0.453 | 0.485 | 1364 |
    | 2 | 0.276 | 0.298 | 682 |
    | 4 | 0.146 | 0.168 | 341 |

  - **Lever (b) RETAINED.** The 3×3 soft PCF receive kernel is untested but
    unrefuted, cheap, and independent of the honeycomb. Keep it as the S3 work
    item. **Keep S1's splat/bias math untouched throughout (D4).**
  - **Architecture for finer on-screen resolution (Q2 → d): DEFERRED.**
    Re-measure on the post-#2385 baseline before committing to any structural
    change. The far cascade (serving iso-depth ≈ 0 receivers while pinned to
    its full slab) is the limiter, so materially finer on-screen shadows need
    an architecture change, not an extent tweak. Non-binding, cheapest-first
    order for the post-#2385 re-measure: (c) content-fit split retune that
    keeps a full-slab fallback (no buffer/UBO cost) → (a) 2048² sun map (≈4×
    sun-map memory; check the Metal buffer-budget note before scoping) → (b)
    3rd cascade (UBO churn + 3× buffer + receiver-selection change + extra bake
    pass — most invasive). **If the post-#2385 metric at zoom 1–2 already reads
    clean, close the "finer resolution" want with a measurement citation
    instead of building any of (a)–(c).**
- **Supersedes:** the plan's lever-(a) framing (now measured-refuted) and the
  blind "≥2×" acceptance ratio (it was authored against a honeycomb-dominated
  metric).
- **Acceptance criteria:** re-anchor the S3 gate as **"material, measured
  improvement in render-shadow-metric components / largest_frac at zoom 1–2,
  with the numeric target set from the FIRST post-#2385 baseline capture"** —
  the resuming worker re-derives the target from the clean (post-#2385) oracle
  rather than inheriting the ≥2× number calibrated to a defect that no longer
  exists. Zero-caster flat floor stays 0 shadow px (D5 primary gate). Keep S1
  splat/bias untouched (D4). Verify cardinal + ~30° + 45° yaw on both backends
  (D3) via the V3 light-verify harness (#2317). The zoom-matrix shadow framings
  already added in PR #2393 are additive measurement infra and stand.
- **By:** epic-steward — source: opus-architect answer on the #2314 STEWARD
  PROPOSAL 2026-07-14 thread
  (issuecomment-4977022751, 2026-07-15) answering the worker NEEDS-DESIGN on
  PR #2393; measurement table from the worker's Metal A/B in that NEEDS-DESIGN.

### A2 — 2026-08-08 — trigger: PR #2654 merged (#2385 Phase-0 r7 on master `34c7f7f4`)

- **Decision (narrow, and deliberately only half the question).** A1 re-anchored
  this issue's gate to "a material, measured improvement in components /
  largest_frac at zoom 1–2, numeric target from the **FIRST post-#2385 baseline
  capture**". #2385's Phase-0 (the r7 radius bump) is now **on master**, but
  #2385 itself is **still open**, so "post-#2385" has two possible referents. The
  half that is unambiguous, and all this amendment claims: **any baseline capture
  used to set this issue's numeric target is valid only if taken at master
  ≥ `34c7f7f4`.** A capture from before that commit measures the pre-r7 honeycomb
  and would re-calibrate the gate against exactly the contamination D7/D9 moved it
  off.
- **The other half is not decided here.** Whether more of #2385 must land before
  the baseline is taken — i.e. whether r7 discharges **D6** or a successor child
  follows — is the open question on umbrella #2314
  (`## STEWARD PROPOSAL 2026-08-08`, `fleet:steward-proposal` applied). The
  steward is not guessing it: this issue stays `fleet:blocked` on #2385 per **D7**,
  and whoever distributes that ruling amends this file again with the referent
  settled.
- **Supersedes:** nothing — additive. A1's lever set is untouched: lever (a)
  (zoom-aware near-cascade extent fit) stays DROPPED as measured-refuted, lever
  (b) (3×3 PCF receive kernel) stays RETAINED, and S1's splat/bias surface stays
  off-limits per **D4**. PR #2654 renamed and removed no symbol this plan cites
  (it touched `system_bake_sun_shadow_map.hpp` and the two `ir_sun_projection`
  twins; this plan's surfaces are the receive kernel and the cascade extents), so
  no other part of the plan is stale.
- **Acceptance criteria:** unchanged in substance; the baseline-capture clause
  gains the `≥ 34c7f7f4` floor above. **D8** still reserves the "materially finer
  on-screen shadows" architecture judgment (2048² map / 3rd cascade / content-fit
  split retune) for the post-#2385 re-measure — that judgment is not this issue's
  to make either, and if the post-r7 zoom 1–2 metric already reads clean, D8's own
  instruction is to close the "finer resolution" want with a measurement citation
  rather than build any of the three.
- **By:** epic-steward — source: PR #2654 merge commit `34c7f7f4` and its file
  list; epic #2314 ledger D7, D8, D9 and the 2026-08-08 Events entry; `#2321`
  labels re-checked live (`fleet:blocked` present, no `fleet:merger-cooldown` on
  PR #2393).
