# Plan: render: high-zoom stage-1/stage-2 dispatch cost for occupancy-less scenes — cut launched workgroups (micro-slice packing + feeder dispatch partition) [re-plan v2]

- **Issue:** #2258
- **Model:** opus — the design decisions (which lever, dispatch layout, buffer partition scheme, cap derivation) are committed below with verified premises and an explicit mid-implementation measurement gate + STOP rule; execution is bounded. The two prior refutations on this surface came from unmeasured cost models, not implementation subtlety — this plan keys every step on the posted attribution table and bakes in the decisive probe first.
- **Date:** 2026-07-07
- **Blocked by:** #2280 (PR #2288, merged) — the wired `voxelCompact`/`voxelStage2` + narrowed `voxelStage1` sub-stage rows are how the Step-A gate and the acceptance criteria measure. Base the impl branch on master after #2288 merges.

### Decision — what the attribution table rules in and out

The 2026-07-08 attribution table on this thread (via #2280 scopes) plus the two refutation probes pin the zoom-16 ~141 ms to the **stage-1 (64.8 ms) and stage-2 (76.1 ms) dispatch grids** — `voxelCompact` (0.06) and `canvasClear` (0.01) are noise. Established facts the lever must respect (docs/design/gpu-stage-timing-cost-model.md §2):

- Per-invocation **body** work is not the cost: a 256× stage-1 body reduction changed nothing (PR #2266).
- Shader-side early-return cannot reclaim it: stage-2 *already* early-returns every feeder (#1740, `c_voxel_to_trixel_stage_2.glsl:358-371`) and still costs 76 ms — those are launched-and-returning workgroups.
- The launch grid is `compactedCount × effSub²` workgroups of **6 threads each** (`local_size 2×3×1`, one thread per face slot) in BOTH stages: the compact writes `gz = subdivisions²` (`c_voxel_visibility_compact.glsl:172-179`) and each stage consumes it via `dispatchComputeIndirect` (`system_voxel_to_trixel.hpp:1268,1300`). At zoom 16 with the feeder-widened cull swallowing the volume, that is O(10⁷) six-thread workgroup launches per stage per frame.

So the committed lever is to **cut launched workgroups**, two composable ways:

- **Step A — pack micro-grid z-slices into fatter workgroups.** `local_size_z: 1 → 8` with `gz = ceil(zTotal/8)`; each invocation re-derives its micro-slice as `zIdx = gl_WorkGroupID.z*8 + gl_LocalInvocationID.z`. Same invocation set, same bodies, same atomics — byte-identical output, ~8× fewer launches in both stages. This is also the **decisive probe**: if the stage rows don't move ≈×8, the per-launch model is wrong and the STOP rule fires (below).
- **Step B — partition feeders out of the dispatch itself.** The compact classifies each surviving voxel (visible vs feeder) using the `isoPos` it *already computed* for the cull test, appends feeders to the tail of the existing compacted buffer, and writes a **second indirect struct** for them. Stage-2 dispatches **visible-only** (feeders contribute nothing there — the #1740 skip proves byte-identity; now they stop launching). Stage-1 dispatches visible at full `effSub²` plus a second feeder dispatch at `feederSubCap²` **strided** sampling. This is PR #2266's cap moved from the shader body (where it provably cannot work) to the indirect params (where the cost actually is).

**Lever 2 from the issue body (generalize interior occupancy via the binding-28 grid) stays rejected**, on the superseded plan's grounds (no-op for the off-lattice wave scene that is the measured pathology; builds on a doomed, bake-aliased buffer) plus a new one: it cuts compacted *count* only for interior voxels of static lattices — it does not touch the `× effSub²` launch multiplier the table attributes the cost to.

**Why A+B delivers the issue's asymptotic ask:** visible count shrinks ~1/zoom² while `effSub²` grows ~zoom² → visible launches ≈ constant; feeder launches ≈ `feederCount × cap²/8` with cap tracking bake density (zoom-independent at high zoom) → the `effSub²`→~const transition past zoom 4 the acceptance criteria require.

### Verified current state

Confirmed by reading the code on master (4a0934d0):

1. **Both stages are 6-thread workgroups.** `layout(local_size_x=2, local_size_y=3, local_size_z=1)` — stage-1 `.glsl:12`, stage-2 `.glsl:3`, Metal twins likewise; `slot = localIDToFace_2x3(gl_LocalInvocationID.xy)` (stage-1 :336).
2. **Workgroup→voxel mapping:** `compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX` (stage-1 :325, stage-2 :220); micro-cell from `gl_WorkGroupID.z`: `u = z / subdivisions, v = z % subdivisions` (stage-1 :636-637, stage-2 :401-402); per-axis base-res path early-returns `gl_WorkGroupID.z != 0` (stage-1 :586, stage-2 :335).
3. **The compact writes the grids GPU-side** in `writeDispatchDims` (`c_voxel_visibility_compact.glsl:172-179`): `gx = min(count,1024)`, `gy = ceil(count/gx)`, `gz = (voxelRenderOptions.x != 0) ? sub² : 1`. Single-list mode appends via `atomicAdd(params[3])` (:239-241); per-axis split mode writes **three structs at `kPerAxisIndirectStrideUints = 64` uints (256 B) apart** (:75, :277-279) — the multi-struct layout + offset-dispatch precedent Step B reuses (`dispatchComputeIndirect(indirectBuf_, offsetBytes)` already used per-axis, `system_voxel_to_trixel.hpp:593-632`).
4. **Feeder classification input is free.** The compact already computes `isoPos` per surviving voxel for the widened-bounds test (:194-223). `visibleIsoBounds_` sits in the SAME binding-7 UBO at offset 176 (`ir_render_types.hpp:486`, static_assert :753) — the compact's block declaration just extends to include it; no C++ upload change. Stage-1 already declares it (:81).
5. **Three spare UBO lanes exist**: `resolveModePad0_/1_/2_` at offsets 196/200/204 (`ir_render_types.hpp:503-505`) — exactly enough for `feederSubCap_`, `feederPassTailBase_`, `feederPass_` without growing the struct or touching the full bind budget (0–30, no new binding anywhere in this plan).
6. **Mid-tick UBO rewrite is established practice**: `frameData_.perAxisRoute_` is rewritten + re-uploaded between dispatches in the same per-canvas tick (`system_voxel_to_trixel.hpp:1193-1194, 1249-1250`) — the same mechanism flags the feeder dispatch.
7. **Bake-density inputs exist**: `sunBakeFrustumUVBounds` (extracted for exactly this purpose by the narrowed PR #2266, commit 10fa23de) in `system_bake_sun_shadow_map.hpp`; `kSunShadowMaxDistance` in `sun_shadow_constants.hpp`; the 1024² map is fixed (render CLAUDE.md §"Sun shadow bake AABB sweep").
8. **Baselines**: pre-#2280 bundle 47.6/145.2 ms at zoom 8/16; post-#2280 split 23.7+28.1 / 64.8+76.1; occupancy-armed target curve 6.1/12.6 (`--wave-mode rigid`). Run-to-run noise ±10 ms at the 140 ms scale (cost-model doc §3.3).

### Approach (committed; two steps with a measurement gate between)

**Step A — micro-slice packing (byte-identical, both stages, both backends).**

1. Add `kStageMicroSlicesPerGroup = 8` to `ir_constants.glsl` + the Metal shared header (single source per backend; the compact's `writeDispatchDims` and both stage kernels must all use it — a mismatch silently drops or double-runs micro-slices).
2. Stage-1 + stage-2 (GLSL + Metal): `local_size_z = 8`; compute `zIdx = gl_WorkGroupID.z * 8 + gl_LocalInvocationID.z`; `zTotal = (voxelRenderOptions.x != 0) ? subdivisions*subdivisions : 1`; top-of-kernel `if (zIdx >= zTotal) return;`; replace every `gl_WorkGroupID.z` read with `zIdx` (the :586/:335 per-axis z==0 checks and the :636-637/:401-402 u/v derivations).
3. Compact (GLSL + Metal): `writeDispatchDims` writes `params[base+2] = divCeil(zTotal, 8)`. Applies to the per-axis structs too (their z≥1 early-outs keep identical semantics; the launch reduction there is incidental — see Gotchas).
4. Metal: update the threadgroup-size map entries for both stage kernels to (2,3,8) — an unmapped/stale entry dispatches the wrong shape silently.
5. **Gate (mandatory, posted to the PR):** `fleet-run IRPerfGrid --auto-profile 120 --zoom 8` and `--zoom 16` (default wave scene, shadows on), read the #2288 sub-stage rows, repeat runs per the noise rule. **Expected if the per-launch model holds: stage-1 and stage-2 rows drop ≈×8.** **STOP rule:** if the drop is <×2, the cost is per-invocation, not per-launch — do NOT proceed to Step B on a dead premise; post the measured table on this issue, swap the task back to `fleet:needs-plan` with the finding, and ship Step A alone only if it is a clean win (byte-identical + any measured improvement), else close the PR.

**Step B — feeder dispatch partition (single-list mode only).**

6. **Compact classification + two-ended append.** In the `perAxisSplitStride == 0` branch (:224-241): after the existing cull + interior-drop logic, test the already-computed `isoPos` against `visibleIsoBounds` (same containment convention as stage-2 :370-371). Visible → forward append at `params[3]` exactly as today. Feeder → tail append: `slot = atomicAdd(params[base1 + 3], 1)` (struct 1's count slot, `base1 = kPerAxisIndirectStrideUints`), store at `compactedVoxelIndices[uint(voxelCount) - 1u - slot]`. No overflow by construction: `nVisible + nFeeder ≤ compacted survivors ≤ voxelCount ≤ buffer capacity`. Struct 1 usage is mutually exclusive with per-axis split mode by the branch itself.
7. **Compact dims:** struct 0 from the visible count with `zTotal = sub²` (as Step A); struct 1 from the feeder count with `zTotal = feederSubCap²` (cap arrives via the UBO lane, below).
8. **CPU cap derivation** (`system_voxel_to_trixel.hpp` beginTick, beside the `visibleIsoBounds_` fill ~:900): `bakeTexelsPerWorldUnit = 1024.0 / maxExtent(sunBakeFrustumUVBounds(...))`; `feederSubCap = IRMath::clamp(int(IRMath::ceil(bakeTexelsPerWorldUnit * kFeederSubSafetyFactor)), 1, effSub)` with `kFeederSubSafetyFactor = 1.0` as the ONE render-debug-loop-tunable knob (widen only if validation shows holes). Sun shadows OFF ⇒ `visibleIsoBounds == cull bounds` ⇒ zero feeders appended ⇒ the feeder dispatch is empty and the path disarms structurally. Fill the three spare lanes: `feederSubCap_`, `feederPassTailBase_ (= effectiveVoxelCount)`, `feederPass_ (0)` — rename the `resolveModePad*_` fields, update the sizeof/offset static_asserts' text.
9. **Stage-1 second dispatch:** in the `!skipSingleCanvasVoxels` block, after the existing visible dispatch: rewrite `feederPass_ = 1` + `subData` (the :1193/:1249 precedent), `dispatchComputeIndirect(indirectBuf_, kPerAxisIndirectStrideUints * sizeof(uint32_t))`, restore `feederPass_ = 0`. Barrier placement mirrors the existing stage-1→stage-2 image barrier (feeder pass writes the same distance image; keep one barrier after both stage-1 dispatches).
10. **Stage-1 shader feeder path:** when `feederPass != 0`: `voxelIndex = compactedVoxelIndices[uint(feederPassTailBase) - 1u - compactedIdx]`; `if (zIdx >= feederSubCap*feederSubCap) return;` **strided micro-grid** — `u = (zIdx / feederSubCap) * subdivisions / feederSubCap; v = (zIdx % feederSubCap) * subdivisions / feederSubCap;` (integer form `(i * subdivisions) / feederSubCap` — monotone, spans the full face; `cap == subdivisions` degenerates to the identity mapping). ALL geometry stays in `subdivisions` units — `voxelPositionFixed`, `frameOffsetFixed`, `encodeDepthWithFace` scale unchanged; the surviving samples are a strict strided SUBSET of the micro-cells the full pass would write (a genuinely coarser sampling of the same face — NOT #2266's corner-block early-return, which sampled only the `[0,cap)²` corner).
11. **Stage-2 dispatches struct 0 only** (no code change beyond Step A — it already reads offset 0). The #1740 in-shader skip STAYS as the safety net for classification drift.

### Affected files

- `engine/render/src/shaders/c_voxel_visibility_compact.glsl` + `metal/c_voxel_visibility_compact.metal` — visible/feeder partition, struct-1 dims, `divCeil(zTotal, 8)`; extend the UBO block declaration through the appended fields.
- `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl` + `metal/c_voxel_to_trixel_stage_1.metal` — `local_size_z=8` + `zIdx` derivation; feeder-pass tail read + strided micro-grid.
- `engine/render/src/shaders/c_voxel_to_trixel_stage_2.glsl` + `metal/c_voxel_to_trixel_stage_2.metal` — `local_size_z=8` + `zIdx` derivation only.
- `engine/render/src/shaders/ir_constants.glsl` (+ Metal shared constants header) — `kStageMicroSlicesPerGroup`.
- `engine/render/include/irreden/render/ir_render_types.hpp` — rename the three `resolveModePad*_` lanes to `feederSubCap_` / `feederPassTailBase_` / `feederPass_`; sizeof unchanged (assert text update only).
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — cap derivation in beginTick; feeder dispatch + `feederPass_` rewrite in the per-canvas tick.
- Metal threadgroup-size map (`metal_runtime.hpp` / wherever the stage kernels' entries live) — (2,3,8) for both stage kernels.
- *(Reference only, do not modify)* `system_bake_sun_shadow_map.hpp` (`sunBakeFrustumUVBounds`), `sun_shadow_constants.hpp`, `c_voxel_to_trixel_stage_2.glsl:358-371` (the feeder-skip convention to match).

### Acceptance criteria

- **Step A byte-identity:** cardinal deterministic shots `cmp`-identical pre/post across the zoom sweep (1/4/8/16), sun shadows ON and OFF (`perf_grid --auto-screenshot --no-overlay`). Per-axis (off-cardinal) poses: visual check only — the per-axis path is run-to-run non-deterministic at fixed pose (#2255), byte-compare is invalid there.
- **Step A gate table posted on the PR** (zoom 8/16, repeat runs, ±10 ms noise bound stated), with the ×-drop called out.
- **Step B visible-region byte-identity:** the visible partition renders at full `effSub²` through the same code path — same `cmp` matrix as Step A must still pass with the partition armed.
- **Sun-shadows-OFF byte-identity:** zero feeders ⇒ structurally disarmed; verify with the shadows-off shots.
- **Perf:** zoom 8/16 `voxelStage1`+`voxelStage2` rows land toward the occupancy-armed band (6.1 / 12.6 ms) — full parity not required, but the `effSub²`→~const transition past zoom 4 must be visible in the table (visible launches ~const, feeder launches cap-bound).
- **Shadow quality:** `render-debug-loop` across the zoom sweep with an off-screen caster shadowing on-screen geometry — feeder shadow edges may coarsen (documented, accepted); holes/dropouts may NOT appear. Holes ⇒ bump `kFeederSubSafetyFactor`, re-verify (the intended tuning loop). Attach before/after + an ROI crop of an off-screen-caster shadow boundary.
- Both backends build; shader change ⇒ `fleet:needs-linux-smoke` on approval (authored on macOS).

### Gotchas

- **Per-workgroup vs per-invocation ambiguity is the residual risk — the Step-A gate is the decisive probe.** Honor the STOP rule; do not rationalize a <×2 result and continue.
- **`kStageMicroSlicesPerGroup` must match in FIVE places** (2 stage GLSL, 2 stage Metal via the shared header, compact `writeDispatchDims` ×2 backends) AND the `local_size_z` literals AND the Metal threadgroup map. A silent mismatch drops micro-slices (visual gaps) or double-runs them.
- **Metal threadgroup-size map**: kernels without a correct entry dispatch 1×1×1 threadgroups — the change silently reverts to per-launch hell. Verify the map entry lands with the shader edit.
- **Tail indexing off-by-one**: feeder slot `i` lives at `voxelCount - 1 - i`; `feederPassTailBase_` must be the SAME `effectiveVoxelCount` the compact was dispatched with (the inverse-resample dest-slot path also sets `frameData_.voxelCount_` to it — reuse that value, don't re-derive).
- **Classification must be the compact's own `isoPos`** — do not re-derive with a second rotate/shift sequence. Stage-2's :370-371 skip (kept as safety net) uses the same containment convention; a voxel the compact calls visible but stage-2 calls feeder just pays the old cost, never corrupts output.
- **Strided u/v integer math:** use `(i * subdivisions) / feederSubCap` (monotone, full-span). `cap == effSub` must degenerate to identity (the disarm path).
- **Geometry scale unchanged on the feeder pass** — only sampling density drops. Positions/depth stay in `subdivisions` units; a cap-scaled `voxelPositionFixed` would write wrong depth for the bake.
- **Struct-1 is per-axis territory in split mode** — the feeder partition exists only in the `perAxisSplitStride == 0` branch; assert/comment the mutual exclusion rather than sharing.
- **Per-axis is out of scope (#2281/#2292 lane).** Step A incidentally cuts per-axis store launches (its z≥1 early-outs pack too) — fine, but do no per-axis tuning here; note it so #2281's table is re-measured after this lands.
- **#2270 / PR #2293 boundary (design-blocked, bake under-coverage):** both concern sparse canvas depth vs sun-map density. This task must not push feeder density below what the quality gate demonstrates hole-free with the CURRENT bake; if #2293's coverage-splat lands later, the safety factor can be revisited downward. No file overlap (bake shaders untouched here).
- **Fog interactions are upstream of the partition** — vision-circle keeps and the interior-drop gate run before classification; feeders are off-screen by definition, vision circles on-screen. No new interaction.
- **Plans are engine-public** — kept to engine terminology.

### Sibling / in-flight reconciliation

- **#2280 / PR #2288** — dependency for measurement rows; base on master after merge.
- **#2281 / PR #2292** (design-blocked) — owns the per-axis yaw delta; disjoint lever (cardinal zoom cost here). Step A's shared-shader packing incidentally helps its surface; its attribution table should be re-taken after this lands.
- **#2270 / PR #2293** (design-blocked) — bake coverage of sparse depth; boundary stated in Gotchas.
- **PR #2266** — refuted the in-shader cap; its salvage (`sunBakeFrustumUVBounds`) is a direct input to Step B's cap derivation.
- **#2254 / PR #2273** — the indirect-dispatch conversion this builds on; #2273's own A/B (perf-neutral) is consistent with this plan: it kept the launch count identical, this plan is about reducing it.
- **#2256** — closed via #2273; successor is #2281 (above).

### One task or subtasks

One task, one PR. Step A and Step B are sequential commits inside it with the measurement gate between them; the STOP rule escalates back to `fleet:needs-plan` rather than deferring any design choice to mid-implementation improvisation.
