## Plan

**State correction (2026-06-27):** the prior claim (PR #1742, mac-opus-worker-1, 2026-06-12) was an empty claim stub — zero work commits, idle 15 days. Closed + claim released; re-planning here so the next worker can finish it through review. Blocker #1720 (GRID coverage) is CLOSED — the geometry-hole prerequisite is met.

**Relevance (still relevant; now part of the #1717 consolidation):** #1718 is the *receiver-side / lighting* facet of the rotating-solid banding. It overlaps #2010, which posits the same venetian banding is a *geometry* face-exposure-mask defect (confirmable via `--no-lighting`). These are competing diagnoses of the same symptom — so step 1 is the disambiguation, which is exactly #2010's confirmation spike.

**Picked approach (verify-first; cf. #1457 mis-diagnosis history):**
1. `--no-lighting` A/B on the banded poses. If the banding disappears unlit → lighting facet (this issue). If it persists unlit → it is the #2010 geometry face-mask; defer the fix to #2010 and reduce #1718 to whatever residual lighting banding remains.
2. For the lighting residual: confirm the detached world-receive path (P4b-2 `ir_sun_shadow_sample` callers) applies the same normal-offset / slope-scale bias (#444) the main `COMPUTE_SUN_SHADOW` path applies — a missing bias on one caller exactly reproduces per-path banding.
3. If bias is present but insufficient for cell-stepped geometry at grazing sun: add a receiver-side depth tolerance ~1 cell (slope-aware), designed once and shared, not per-symptom.

**Coordination:** receiver-side; complementary to the caster-side C-series (#2081 deform, #2082 coverage). Sequenced-in-spirit on #2010's disambiguation but NOT hard-blocked — the verify step IS the disambiguation, so it can proceed now. Do not duplicate #2010's geometry work.

**Files:** `ir_sun_shadow_sample.glsl` (bias parity) + metal twin; `c_compute_sun_shadow.glsl`; `c_lighting_to_trixel.glsl` world-receive; `system_compute_sun_shadow.hpp`.

**Acceptance:** a rotating re-voxelize / GRID cube under the canvas_stress sun shows per-face shading (no row banding) across a full spin; both backends; `--no-lighting` stays byte-clean.

Plan file: `~/.fleet/plans/issue-1718.md`.
