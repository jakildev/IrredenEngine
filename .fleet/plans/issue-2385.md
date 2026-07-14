# Plan — issue #2385

## Plan status: STUB — needs planning before claim

**Part of epic:** #2314 (render: lighting/shadow domain culling correctness + validation infrastructure)
**Model:** opus
**Blocked by:** (none)

Stub committed by epic-steward on flow-c adoption (2026-07-14). This child was
filed as the D6 discharge from epic #2314's S1 (#2319 / PR #2343) post-merge
reconcile: the same-plane provenance test S1 landed unmasked a genuine-cast
under-coverage residual that is **#2270-lineage splat coverage** (in-map cast
extent), **not** receive-side correctness. Per D6 it is accepted for #2319 and
re-fixed here on the decontaminated baseline.

Measured basis to plan against (from PR #2343, macOS/Metal):
- S1 same-plane cast ROI: 24400 px / 59 components / 0.7705 coverage.
- splat-off master lower bound: 5056 px / 93 components / 0.3418 coverage.

The gap between these is the genuine-cast coverage this child must close without
reintroducing the coplanar self-hit that S1 removed (D4/D5). Re-ground S-series
acceptance on the decontaminated oracles per D5, and verify at cardinal + ~30°
+ 45° yaw on both backends per D3 (the V3 light-verify harness from #2317 —
`scripts/light-verify.py` domain matrix — is the acceptance oracle).

An opus planner must expand this stub into a full `## Plan` before a worker
claims #2385. Do not queue until planned.
