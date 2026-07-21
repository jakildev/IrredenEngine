## Plan: render: voxel cylinder yaw-sweep centroid JITTER on master (reversals=9, 2.9px residual)

- **Issue:** #2427
- **Model:** fable
- **Date:** 2026-07-20

### Scope

Restore SMOOTH on the canonical rotation-jitter gate (voxel cylinder `--yaw-sweep`, jitter_probe reversals=0 / residual ≤ 1.5px at zoom 2/4/8) by fixing the θ-unstable view-visibility membership in the per-axis overflow lane (#2333/#2357), which this plan identifies as the regression the issue reports.

### Verified current state (planner measurements, 2026-07-20, macOS)

Current master is unbuildable on this host (#2449), but a coherent Jul-8-era stack (binary + era-matched staged shaders at `9568bb85`; Metal loads `.metal` at runtime) ran the issue's exact repro plus discriminating variants:

| stack / variant | verdict | x | y |
|---|---|---|---|
| issue (filer): master `4cef76bb`, current build | JITTER | rev=9, resid=2.93px, Δmax=5.37 | rev=0, resid=0.36px |
| Jul-8 stack (`9568bb85`), exact repro | JITTER | rev=12, resid=0.51px, Δmax=0.84 | rev=3, resid=0.53px |
| Jul-8 stack + `--pivot-origin` | JITTER | rev=8, resid=0.52px | rev=4, resid=0.58px |
| Jul-8 stack + **current shaders staged** (hybrid) | JITTER | rev=12, resid=0.51px | rev=3, resid=0.53px |
| Jul-8 stack, SDF cylinder (no `--spin-shape-voxel`) | SMOOTH | rev=0, resid=1.43px | rev=0, resid=0.95px |

Conclusions, each grounded in a row above or a code citation:

1. **The multi-pixel signature (2.9px residual / 5.4px deltas) is a regression introduced in `9568bb85..4cef76bb`** — it does not exist on the Jul-8 stack, which shows only a 0.5px reversal wobble.
2. **Issue candidate B (#2418 wFrac per-bracket rounding) is refuted for the multi-pixel class:** the #2418 encoding is shader-internal (stage-1 encode → scatter decode, both in the staged set), and the hybrid row (current shaders incl. #2418 on the Jul-8 binary) is metric-identical to the Jul-8 baseline. (Caveat: shader features gated on new C++ wiring are dormant in the hybrid — the encoding is not one of them.)
3. **Window audit** (commits in `9568bb85..4cef76bb` touching this path): `40a4da46` (#2278 per-voxel Hi-Z cull) is **excluded** — gated to cardinal poses (commit message: "Set only on the states the chunk pre-pass is verified for (… cardinal …)") and the chunk pre-pass is `--occlusion-cull` opt-in, off in the repro. `e6c074bc` (#2348) touches the detached composite, not the per-axis GRID path the probe exercises. `7c652ab8` (#2406 wrapToRange) has no wrap boundary inside the 0.05–0.70 rad sweep. **Remaining: `dbe7af3b` — the #2333/#2357 view-visibility overflow lane**, active exactly under residual yaw, whose membership is face-selective by orientation.
4. **Mechanism (code-level, `c_voxel_to_trixel_stage_1_body.glsl` `overflowAppendTap`):** a face's append membership flips discretely as yaw steps — (a) its mask cell is `roundHalfUp(pos3DtoPos2DIsoYawed(facePos, visualYaw))`, so a face near a cell boundary alternates which cell's winner it competes against; (b) its key is `floor(yawedIsoDistance·16)`, hopping brackets. A near-tie face flips appended/dropped frame-to-frame; each flip pops a whole face quad (several px at zoom 4). Only **lateral (X/Y side) faces** reclassify under Z-yaw — the Z-top face never does — which explains the x-only, y-clean signature. The in-code comment sanctions the fix direction: *"Over-emit is safe — the framebuffer depth test cleans up; under-emit re-opens the #2331 holes."*
5. The filer's y row (reversals=0, 0.36px on current master) is *stiller* than the Jul-8 y (3 reversals) — consistent with #2418 having fixed the old out-of-plane-reconstruction wobble in the same window. Expected consequence: killing the overflow flicker meets criterion 1 in full; phase 0(b) verifies this before any code is written.
6. The SDF row (SMOOTH) plus the pan-sweep twin (SMOOTH per the issue) isolate the defect to the per-axis voxel path under yaw — nothing camera- or blit-side.

### Approach

**Phase 0 — premise confirm (cheap, before any edit; needs a host that can build current master — Linux/Windows until #2449 lands):**
- (a) Exact repro on current master → expect the issue metrics (x rev≈9, resid≈2.9px, zoom 4). Positive control.
- (b) Same repro with `IR_PERAXIS_OVERFLOW_DISABLE=1` (the #2333 kill switch, `system_trixel_to_framebuffer.hpp:491`) → **expected reading: x collapses to the sub-pixel envelope (resid ≤ ~0.6px) with reversals ≈ 0.**
- **Bail path:** if (b) does not collapse the signature, the premise is refuted — comment both probe outputs on the issue, bisect `9568bb85..4cef76bb` with the same gate (4 candidate commits ⇒ ≤3 steps), and flag for re-plan. Do not build phase 1 on a refuted premise. (The kill switch is diagnosis only — leaving the lane off re-opens the #2331 dropped-coset holes.)

**Phase 1 — θ-stable overflow membership (the fix):**
In `overflowAppendTap` (GLSL + MSL twins), replace the single-cell mask compare with a **footprint-spanning any-accept test**: accept iff the face's key is within `kOverflowDepthEpsSteps` of the **most permissive (largest) mask winner over the 2×2 cell neighborhood spanning the unrounded yawed position** (`floor(p)` and `floor(p)+1` per axis). A cell hop then no longer changes the comparison discontinuously — acceptance varies with the winner landscape the face's footprint actually straddles, and errs toward append (the sanctioned over-emit direction; over-emit cannot re-open #2331 holes, only under-emit can).
- Keep `overflowYawedDepthKey` and the mask **write** side untouched — the write/compare self-tie symmetry ("a face always ties its own mask entry exactly") must be preserved; the neighborhood applies to the **compare side only**, and any-accept (max) can only admit a superset, so self-tie still passes.
- If the probe still shows residual flips at any zoom, widen the compare span to the face quad's projected-AABB cell span (≤3×3 at the base-resolution store; a unit face spans ≤ √3 iso cells) — same mechanism, bounded escalation gated by the named acceptance test. This is a dial inside the one approach, not an approach fork.
- Watch the overflow cap: any-accept admits more entries; the drop counter (`ctrlBase+5`, CPU one-shot warn) must stay silent through the acceptance matrix — if it fires, bump the scratch sizing (`overflowScratchLayout_`, `voxel_frame_data.hpp`) in the same PR.
- Cardinal byte-identity: check the overflow dispatch site — if resolveModes 2/3 already skip at `residualYaw == 0`, identity holds by construction; if they run, the acceptance img_diff=0 gate catches any drift (an admitted-at-cardinal face is occluded by its cell winner and loses the depth test, so no visible delta is expected).

**Phase 2 — acceptance matrix** (below) on the same host; attach probe outputs pre/post to the PR.

### Affected files

- `engine/render/src/shaders/c_voxel_to_trixel_stage_1_body.glsl` — `overflowAppendTap`: footprint-spanning any-accept mask compare
- `engine/render/src/shaders/metal/c_voxel_to_trixel_stage_1_body.metal` — MSL twin, bit-parity mirror
- `engine/prefabs/irreden/render/voxel_frame_data.hpp` — only if the overflow-cap drop warn fires (scratch sizing bump)
- `.fleet/plans/issue-2427.md` — this plan, first commit of the implementation PR

### Acceptance criteria (positive-fire)

1. **Positive control fires:** phase 0(a) reproduces the defect on current master (x reversals ≥ 1, resid > 1.5px) before the fix; phase 0(b) collapse confirms the mechanism.
2. **On-path delta observable:** per-frame overflow append count across the sweep oscillates step-to-step pre-fix (the flicker, read via the ctrl-block counter or a temp log) and varies smoothly post-fix — the fix demonstrably changes the enabled path, not just the default.
3. Exact repro → **SMOOTH at zoom 2, 4, 8** (reversals=0 both axes, resid ≤ 1.5px).
4. Pan-sweep twin (`--spin-shape box --spin-shape-voxel --pan-sweep --yaw 0.785 --zoom 4`) stays SMOOTH.
5. Cardinal byte-identity: shape_debug suite pairwise img_diff = 0 at yaw 0.
6. `ir-run RESULT=CLEAN`; overflow-cap drop warning absent throughout.

### Gotchas

- **Never fix this by widening `kOverflowDepthEpsSteps`** — that moves the flicker boundary without removing it and admits occluded coset losers (the stipple class the lane's depth-bias manages).
- Any change that rejects more faces than today is an under-emit regression (#2331 holes). The fix must only ever admit a superset.
- The mask **write** path and `overflowYawedDepthKey` are shared write/compare contracts — compare-side change only.
- MSL twin must mirror the GLSL bit-exactly; Metal-host verification is blocked (#2449) — land GLSL-verified, MSL by review parity, macOS validation via the standard `fleet:needs-macos-smoke` flow.
- The planner's Jul-8-stack staged-shader technique is **not** valid for verifying the fix — the overflow lane needs current C++; build current master.
- Clean the screenshots dir between probe runs (the sequence glob must contain exactly one sweep).
- jitter_probe's reversal deadband is 0.10px/step (`--reversal-eps`) — reversals=0 requires the centroid still to a tenth-pixel per step, which current master's y axis already achieves; it is a real, meetable bar.

