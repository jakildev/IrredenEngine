## Plan: render: per-axis scatter sub-pixel centroid residual under yaw-sweep — accept as measured probe floor; re-parameterize the zoom-4/8 gate

- **Issue:** #2469
- **Model:** opus
- **Date:** 2026-07-20

### Scope

Resolve the issue's chase-vs-accept question: **accept** the zoom-4/8 sub-pixel residual as the probe's own floor (voxel-content anisotropy + raster-sampling noise, not a pipeline positioning defect), document it as intentional drift next to the #1883 precedent, and re-parameterize the canonical voxel-cylinder rotation-jitter gate so it encodes the accepted floor while still firing on every real defect signature the gate exists to catch. No shader or C++ behavior changes ship — the diff is docs + gate parameters, gated by a phase-0 measurement that converts the accept premise from inspection to proof.

### Verified current state (planner, 2026-07-20 — source inspection + cited measurements)

Baseline metrics (issue table, measured during #2427 on macOS/Metal on the #2465 base, post-fix):

| zoom | x rev | x resid | verdict | resid ÷ zoom (world units) |
|---|---|---|---|---|
| 2 | 0 | 0.64px | SMOOTH | 0.32 |
| 4 | 2 | 1.01px | JITTER (sub-px reversals) | 0.25 |
| 8 | 0 | 1.57px | JITTER (0.07px over bar) | 0.20 |

Three source-level findings that reframe the issue's "likely surface" (the 4-bit face-frac encoding + `emitDeformedFace` rounding):

1. **The 4-bit frac lane is inert on this exact probe.** The canonical probe's cylinder is `createVoxelPoolShape(vec3(0), CYLINDER, …, halfExtent (4,4,4))` (`creations/demos/shape_debug/main.cpp:898`, spawn at `:1319`): an odd-size (9³) origin-centred grid, so every active voxel sits on the exact integer lattice, and `--yaw-sweep` steps only the camera yaw (`main.cpp:271` — position + zoom fixed, shape static). The per-axis store computes `fracInCell = worldAligned − worldPos` after `snapNearIntegerVoxelPosition` (1e-4 snap, `ir_iso_common.glsl:450`), so `fracInCell ≡ 0` and `fracToFrac4` encodes exactly 8/8/8 (`ir_iso_common.glsl:279`) — the documented "integer content encodes zero offsets, byte-identical" case. A quantization lane pinned at its zero point cannot produce a yaw-varying wobble.
2. **`emitDeformedFace` is not on this path.** The T3 per-axis store writes ONE cell per face centre (`c_voxel_to_trixel_stage_1_body.glsl` per-axis branch, ~line 690–790); `emitDeformedFace` (~line 329) serves the single-canvas/world and detached routes only.
3. **What actually varies under the sweep** — the cardinal store is yaw-independent within one quadrant and the scatter reprojection (`pos3DtoPos2DIsoYawed`) is continuous and linear in yaw, so the remaining yaw-varying terms are: (a) pixel-coverage sampling (quad edges crossing fragment centres discretely as the projection moves), (b) boundary-pixel arbitration (conservative dilation margin, margin-yield, tie-bands), and (c) the content itself — a voxelized cylinder is only 4-fold symmetric; only the *continuous* cylinder is Z-yaw-invariant, so the true silhouette of the rotating staircase legitimately wobbles.

The measured scaling fits (c)+(a): resid/zoom ≈ 0.32 → 0.25 → 0.20 world units — approximately constant in **world** space (content anisotropy of order ~¼ voxel plus a fixed sub-pixel sampling floor). A pixel-domain positioning defect would sit ~constant in px across zooms; a compounding store defect would scale super-linearly. Neither matches. The defect class this gate exists for looks nothing like the residual: the pre-#2427 overflow flicker measured resid 2.93px / Δmax 5.37 at zoom 4.

Independent control already on record (#2427 plan phase-0 table): the SDF cylinder twin (continuous geometry, no voxel store) sweeps at resid 1.43px x / 0.95px y @ zoom 4 — the defect-free control also sits just under the 1.5px default bar, i.e. the default bar has ~zero headroom over the probe floor.

Sibling/in-flight reconciliation: #2469 is #2427's only carve-off; PR #2471 (the #2427 fix) is approved and unmerged (see Gotchas — base ordering). No open PR touches the scatter positioning surface (#2460 is fog-vision, #2393 is sun-shadow softness). Needs-plan sibling #2360 (Metal sampler binds) is unrelated.

### Approach

**Phase 0 — premise confirm (cheap; base MUST include #2427's fix — see Gotchas):**

- (a) **Frac-lane inertness A/B (runtime proof of finding 1).** Stage a diagnostic copy of the scatter shader with the three decoded sub-cell offsets forced to zero — delete the `eu·(uFrac4/16−0.5) + ev·(vFrac4/16−0.5) + faceOutOfPlaneUnitAxis·(wFrac4/16−0.5)` origin adjustment (`v_peraxis_scatter.glsl` ~line 230; on macOS edit the `metal/peraxis_scatter.metal` twin — Metal loads `.metal` at runtime, so a staged-dir swap needs no rebuild). Run the canonical sweep at zoom 4 and zoom 8; `img_diff` every frame against the unmodified run. **Expected reading: img_diff = 0 for every frame** — the encoding demonstrably contributes nothing on this probe. Diagnostic staging only; nothing commits.
- (b) **Control completion + delta tables.** Run the SDF twin (drop `--spin-shape-voxel`) at zoom 8; expected: the continuous-geometry control's residual also grows with zoom (order ~1px+). Capture the voxel sweeps at zoom 4/8 with `--verbose` — the per-frame delta tables feed the eps pick in phase 2.
- **Bail path:** if (a) shows ANY non-zero frame diff, the frac lane is live on this probe and the accept premise is refuted — stop, comment both probe outputs on the issue, and swap back to `fleet:needs-plan` for a chase-directed re-plan (the live lane then has a measured signature to chase). Same bail if (b)'s SDF control at zoom 8 stays ≤ 0.5px while the voxel path reads 1.57px (a real voxel-path excess, not a shared floor). Never build phases 1–2 on a refuted premise.

**Phase 1 — document the accepted drift.** `engine/render/CLAUDE.md`, adjacent to the #1883 accepted-drift block (~line 741): a titled block "Accepted sub-pixel yaw-sweep centroid drift (voxel content) — #2469" recording the mechanism (voxel content is only approximately Z-yaw-invariant, plus the sampling floor), the measured table with the world-space-constant scaling fit, the phase-0 inertness proof (frac lane contributes zero; `emitDeformedFace` off-path), why there is no local fix (the residual is content + sampling, not a positioning defect; the principled root fix is the same conservative-rasterization/MSAA direction already deferred to epic #1933), and the phase-2 gate parameters.

**Phase 2 — encode the floor in the canonical gate (the issue's "relax to residual-only").** Update the §"Verifying temporal stability" recipe (`engine/render/CLAUDE.md` ~lines 400–435) and `tools/jitter_probe/README.md` (an "accepted floors" note pointing at the CLAUDE.md block). The canonical voxel-cylinder rotation gate becomes:

- zoom 2: unchanged (defaults; measured SMOOTH with 2.3× residual headroom),
- zoom 4: `--reversal-eps 0.5` (max-residual stays 1.5; sub-half-pixel direction flips are the accepted floor — multi-px face-pop reversals still fire),
- zoom 8: `--reversal-eps 0.5 --max-residual 2.0` (measured 1.57 + 27% margin, still 32% below the 2.93px defect signature).

No `jitter_probe` code changes — both flags exist; the budget lives in the documented recipe. The starting eps/budget values above are constrained by acceptance criteria 2 and 3: adjust from the phase-0(b) verbose tables until both hold, and ship the final numbers in the docs.

**Phase 3 — close out.** The PR body records the accept decision (`Closes #2469`) and notes that #2427's own acceptance criterion 3 is superseded at zoom 4/8 by the phase-2 budget — reference only; no edits to #2427 or epic #1881 (the steward's ledger pass picks it up).

### Affected files

- `.fleet/plans/issue-2469.md` — this plan (first commit of the implementation PR)
- `engine/render/CLAUDE.md` — accepted-drift block + updated canonical gate recipe
- `tools/jitter_probe/README.md` — accepted-floors note referencing the block

### Acceptance criteria (positive-fire)

1. **Premise measured, not asserted:** phase 0(a) byte-identity holds — img_diff = 0 for every frame across both zoom sweeps.
2. **Floor passes:** under the shipped recipe, the canonical voxel-cylinder sweep reports SMOOTH at zoom 2, 4, AND 8 (probe outputs pasted into the PR).
3. **Gate still fires on a real lever:** the `IR_PERAXIS_OVERFLOW_DISABLE=1` kill-switch sweep at zoom 4 (re-exposes the θ-unstable membership class; recorded rev=12 / Δmax 0.84) reports JITTER under the exact shipped flags. Hard analytic floor regardless: the pre-#2427 defect record (rev=9, resid 2.93px, Δmax 5.37) fails every shipped setting on the residual axis alone. If no eps satisfies 2 and 3 simultaneously (floor reversals and control reversals overlap in delta magnitude), ship the residual-axis gate, record the runtime control's measured overlap in the CLAUDE.md block as a known limitation, and keep the analytic floor as the hard check.
4. **Untouched twin stays green:** pan-sweep (`--spin-shape box --spin-shape-voxel --pan-sweep --yaw 0.785 --zoom 4`) stays SMOOTH under default flags.
5. Cardinal byte-identity untouched by construction — the shipped diff is docs-only.

### Gotchas

- **Base ordering:** every baseline number is measured on top of #2427's fix. PR #2471 (the #2427 implementation) is approved but unmerged at planning time — if still unmerged at pickup, claim with `--stackable-on 2471` and branch from its head. Running phase 0 on a pre-#2471 base re-exposes the multi-pixel overflow flicker and invalidates every expected reading.
- The phase-0(a) shader edit is DIAGNOSTIC ONLY — never commit it; it deliberately breaks fractional-content rendering (any scene with sub-cell voxel positions). Byte-identity holds only for this probe's integer-lattice content.
- `IR_PERAXIS_OVERFLOW_DISABLE` is a `getenv != nullptr` kill switch — `VAR=` (empty value) counts as SET; fully unset it between runs.
- Keep the sweep inside one cardinal quadrant (the default `--yaw-sweep` range): crossing a quadrant boundary changes the visible-face triplet and legitimately steps the centroid — that is not part of the accepted floor.
- The zoom-4/8 budget is for the CANONICAL cylinder probe only — do not generalize the numbers to other shapes or probes; the floor is content-dependent (the figure fixture or a coarser shape has a different anisotropy amplitude).
- Plans are engine-public: keep all measurements and terminology engine-side.

