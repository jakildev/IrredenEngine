# Plan — #2082: sun-shadow bake analytic coverage (C2) — RE-SCOPED to verify-and-document close-out

**Status:** re-scoped by architect direction on PR #2140 (2026-07-06), superseding
the 2026-06-27 `## Plan` comment on the issue. Child of epic #1717.

## Why the original plan was dropped

The original direction ("extract `scatterAnalyticEdgeCoverage` into
`ir_iso_common`, call it from the bake; thread per-axis frac bits [9:2] through
resolve→bake") failed verify-first on four points (PR #2140 NEEDS-DESIGN,
findings accepted by the architect):

- **F1** — `scatterAnalyticEdgeCoverage` is Metal-only; the GL scatter analytic
  port is the independent, unstarted #1938. Nothing to extract on GL.
- **F2** — the bake is a compute pass (one `atomicMin` per canvas pixel); the
  fragment helper's inputs (`fwidth`, footprint param, neighbour flags) do not
  exist there. "Call from the bake" was a new mechanism, not a helper reuse.
- **F3** — the per-axis resolve already footprint-fills (#1724), and its
  cardinal layout is the bake's exact inverse by design (cast == receive,
  #1380-safe); frac threading contradicts that invariant.
- **F4** — #1784 scope overlap: resolved by closing #1784 as stale (see below).

The decisive cross-lane fact: **PR #2204's deterministic A/B proved a bake-side
footprint splat is a no-op on the acceptance scene on its host** (forced radius
12 byte-identical) — the resolve architecture already delivers dense bake input
on every resolve-fed caster path. #1784 and PR #2204 closed as stale on that
evidence. Architect rulings on PR #2140: no Option-A splat, no #1938
entanglement, no frac threading (accepted ~1-cell residual, #1883 precedent),
#2082 does not close #1784.

## Re-scoped deliverable (this PR)

1. **Deterministic evidence captures** of current cast-shadow quality
   (`--no-auto-rotate --no-spin`, plus `--frozen-pose 0.6` rotated variant):
   canvas_stress `shadow_overlay_floor` + `IRVoxelYaw` (pure-GRID) at cardinal
   and residual yaw. Committed under
   `docs/pr-screenshots/claude/2082-bake-analytic-coverage/`.
2. **Design-doc update** (`docs/design/per-axis-sun-shadow-resolve.md`): the
   resolve fill IS the bake-coverage mechanism (no per-pixel bake splat exists
   or is needed on resolve-fed paths); the accepted ~1-cell per-axis silhouette
   residual; deterministic-pose requirement for shadow-metric acceptance gates.
3. **This plan file** recording the re-scope.
4. **If the evidence run surfaces a real live gap on the cardinal main-bake
   path: file it as a fresh issue** with the deterministic capture instead of
   stretching #2082.

## Evidence-run outcome (macOS/Metal, 2560x1440, this branch = master + claim)

Item 4 fired. The cardinal main-bake path **under-covers the sun map on this
host** — the same splat A/B that was inert in #2204 is strongly non-inert here:

| config | hole_ratio | components | largest_frac |
|---|---|---|---|
| master cardinal | 0.947 | 58 | 0.306 |
| master `--frozen-pose 0.6` | 0.940 | 91 | 0.272 |
| #2204 parity-2× splat | 0.566 | 471 | 0.439 |
| forced splat radius 12 | 0.231 | 4 | 0.948 |

Stable across 69bfb2ea → master head (not a regression; host-conditional).
`IRVoxelYaw` cardinal shadows read moth-eaten in plain render; yaw30/yaw45
(resolve-fed) are dense — the resolve-fill architecture holds. Filed as
**#2270** with repro, scope fences (#2092/#2010 receiver lane untouched), and
acceptance criteria.

## Acceptance for this PR

Docs + captures + plan only — **zero shader / C++ changes**. Build sanity on
the touched targets (captures were produced from clean builds of this branch).
Closes #2082. Does NOT close #1784 (closed separately as stale).
