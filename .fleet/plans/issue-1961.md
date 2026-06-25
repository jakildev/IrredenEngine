## Plan: render: rotation perf parity via per-axis empty-cell compaction (IRPerfGrid cliff) [#1884]

- **Issue:** #1961
- **Model:** opus
- **Date:** 2026-06-24

> 4th planning pass. The prior three correctly deferred under the framing "scatter-side
> lever vs resolve-side lever — can't pick which until #1965 splits composite-vs-resolve
> cost." **That framing dissolves on inspection of the two code paths:** the dominant lever
> attacks the *common driver* of both halves, so it is split-robust and #1965's measurement
> is no longer a planning prerequisite (only a verification aid, folded in below).

### Scope
Close the IRPerfGrid rotation perf cliff documented by #1963: off-cardinal frame
~7.0 -> ~11.5 ms (+4.6 ms, ~1.65x), with **the entire delta in the `trixelToFb` stage**
(0.25 -> 4.91 ms). Keep route 0 (the cardinal special-case) byte-identical -- per the profile
it is a correctness asset, not a speed one (`voxelStage1` is flat off-cardinal, so removing
route 0 buys ~0 ms; that is out of scope here).

### Verified current state (sources read, not guessed)
- **Profile (authoritative):** `docs/design/rotation-perf-cliff-1963-profile.md` (PR #1964,
  merged). Cliff is GPU-bound, a hard binary step at the residual-yaw deadband, 100% in
  `trixelToFb`. Root cause: off-cardinal replaces the single cardinal gather with
  `drawPerAxisScatter` -- a 3-pass forward-scatter over **worst-case-sized** per-axis canvases
  (`IRMath::perAxisTrixelCanvasWorstCaseSize -> ~(2W, W+H)`), "~12x the cardinal scatter area,
  **mostly empty texels**."
- **The cliff's `+4.66 ms` is composite + resolve combined** and the resolve
  (`SYSTEM_RESOLVE_PER_AXIS_SCREEN_DEPTH`) currently reads 0.0 ms because of a registry-name
  mismatch (#1965). The prior passes treated this as a hard gate. It is not -- see below.
- **Both stages are driven by the same empty-cell sweep over the oversized per-axis canvas:**
  - Composite -- `system_trixel_to_framebuffer.hpp:301`:
    `const int instanceCount = axes.size_.x * axes.size_.y;` then `drawElementsInstanced(...)`
    **x3 axes**, "degenerating empties in the vertex shader" (one instance per *cell* over the
    full worst-case grid).
  - Resolve pass 1 -- `system_resolve_per_axis_screen_depth.hpp`: 3 dispatches over
    `axisSize = perAxisCanvases_->size_` (`axisGroupsX/Y = divCeil(axisSize, 16)`), each thread
    `imageAtomicMin` into a main-sized scratch. Pass 2 is a cheap fixed main-size blit.
- **=> Shrinking the non-empty work per axis cuts the variable cost of *both* stages
  proportionally, regardless of which half dominates inside `trixelToFb`.** That is why the
  lever is committable without #1965's split: it is split-robust by construction.
- **Design doc already scoped this exact lever** (`docs/design/per-axis-trixel-canvas-rotation.md`
  sections 149-159, 257-260): "a compute compaction pre-pass that appends non-empty cell indices +
  writes indirect draw args, shrinking the instance count to ~= visible faces" -- deferred only
  because the *light-scene* perf gate (`IRShapeDebug`, 0.078 ms) didn't flag the empty sweep.
  The #1963 **heavy-scene** profile now flags it (the doc's literal trigger: "a heavier scene
  flags it").
- **Infrastructure already exists in this subsystem (both backends):**
  `system_voxel_to_trixel.hpp` already uses `perAxisIndirectBuf_`, `VoxelIndirectDispatchParams`,
  `kBufferIndex_IndirectDispatchParams`, `kPerAxisIndirectStrideUints`,
  `device()->dispatchComputeIndirect(...)`, and the compaction shader `c_voxel_visibility_compact`
  (GL + Metal). The new pass reuses this established pattern -- no new device capability needed for
  indirect *dispatch*; confirm `drawElementsInstancedIndirect` (or an SSBO-count-driven instanced
  draw) is exposed for the composite (gotcha 1).

### Approach (single committed lever -- per-axis cell compaction feeding both consumers)
Build **one** compaction pre-pass over the per-axis canvas *cells* that produces a compacted
non-empty-cell index list + indirect args, then drive **both** the composite and the resolve
pass-1 from it instead of the full `size.x*size.y` sweep.

0. **Fold in #1965** (the resolve-stage timing rename) as the first commit -- trivial and needed
   to *verify* the resolve half drops, not just the composite. Rename registry row
   `screenSpaceResidualRotate` -> `resolvePerAxisScreenDepth` in `gpu_stage_timing.hpp` (the
   `{"screenSpaceResidualRotate", &...ResidualRotateMs_, 0.05f}` row at :230 and the
   `screenSpaceResidualRotateMs_` field at :38) and update the overlay label at
   `system_perf_stats_overlay.hpp:202` (`"screenSpaceResidualRotate"` -> `"resolvePerAxisScreenDepth"`,
   display string e.g. `"PA-RESOLVE"`). Verified: those are the **only** two consumers of the old
   name. PR `Closes #1965` as well.

1. **Compaction pre-pass (new compute shader, GL + Metal).** For each of the 3 axes, scan the
   per-axis distance texture (`axes.axes_[axis].distances_`); a cell is non-empty when its stored
   distance != the empty sentinel (`IRConstants::kTrixelDistanceMaxDistance`). Append each
   non-empty cell's linear index to a per-axis append SSBO via an atomic counter, and write the
   per-axis indirect args: instance count (composite) and dispatch group count =
   `divCeil(count, groupSize)` (resolve). Model it on `c_voxel_visibility_compact` and reuse the
   `perAxisIndirectBuf_` / `VoxelIndirectDispatchParams` layout. Reset the counters each frame
   before the scan; `memoryBarrier` after.
2. **Composite -> indirect instanced draw.** In `drawPerAxisScatter`, replace the
   `instanceCount = size.x*size.y` + `drawElementsInstanced` with an indirect/compacted instanced
   draw whose instance source is the compacted cell list. The scatter vertex shader
   (`v_peraxis_scatter`) currently derives its cell from `gl_InstanceID` -> it instead reads
   `cellIndex = compactedCells[axisBase + gl_InstanceID]` and proceeds identically (same
   `isoPixelToPos3D(cell - perAxisBase, ...)` recovery -- unchanged math, only the cell *source*
   changes). Empty cells never enter the draw, so the degenerate vertex path is removed.
3. **Resolve pass 1 -> indirect dispatch over the same list.** In
   `system_resolve_per_axis_screen_depth.hpp`, replace `dispatchCompute(axisGroupsX, axisGroupsY)`
   with `dispatchComputeIndirect(...)` over each axis's compacted count, and have
   `c_resolve_per_axis_screen_depth.glsl/.metal` read `cellIndex = compactedCells[...]` for its
   `imageAtomicMin` target instead of deriving it from `gl_GlobalInvocationID`. Pass 2 (blit)
   is unchanged.
4. **Re-profile + regression-gate.** With #1965 now observable, re-run the #1963 method
   (`IRPerfGrid --mode dense --grid-size 64 --zoom 0.8 --auto-profile 200`, legacy finish-timing
   for a clean split) and confirm **both** the composite and the resolve drop. Confirm
   byte-identical cardinal (route 0 untouched) and rotated-solidity (#1882) / canvas_stress
   render-verify parity.

### Affected files
- `engine/prefabs/irreden/render/gpu_stage_timing.hpp` -- #1965 registry-row + field rename.
- `engine/prefabs/irreden/render/systems/system_perf_stats_overlay.hpp` -- #1965 overlay label.
- `engine/prefabs/irreden/render/shaders/c_per_axis_cell_compact.glsl` **(new)** + its `.metal`
  twin **(new)** -- the compaction pre-pass; add the Metal twin to `threadgroupSizeForFunctionName`
  in `metal_pipeline.cpp` (else it silently dispatches 1x1x1).
- `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp` -- own the compaction
  dispatch + counter reset; composite indirect draw in `drawPerAxisScatter`.
- `engine/prefabs/irreden/render/systems/system_resolve_per_axis_screen_depth.hpp` -- resolve
  pass-1 indirect dispatch.
- `engine/prefabs/irreden/render/shaders/v_peraxis_scatter.{glsl,metal}` and
  `c_resolve_per_axis_screen_depth.{glsl,metal}` -- read cell index from the compacted list.
- New buffer-index constant for the compacted-cell SSBO (alongside `kBufferIndex_*`), and an
  append-buffer + counter resource owned by the trixelToFb system.
- Shader file-id registration for the new shaders (mirror `kFileCompResolvePerAxis*` /
  `kFileVertPerAxisScatter`).
- `.fleet/plans/issue-1961.md` -- committed as the first impl commit.

### Acceptance criteria
- IRPerfGrid off-cardinal frame cost approaches cardinal cost (close the documented ~4.6 ms
  cliff), measured before/after with `--auto-profile`; the win shows in `trixelToFb` and, with
  #1965, is attributable across composite **and** resolve.
- Cardinal (residual 0) output **byte-identical** to master (route 0 path untouched).
- Rotated frames byte-identical / within existing tolerance: rotated-solidity (#1882) +
  canvas_stress + IRShapeDebug render-verify, **both backends** (GL via Linux smoke, Metal local).
- #1965 resolve stage reports non-zero, separately from the composite, in the GPU stage report.

### Gotchas
- **Backend parity is mandatory.** Every new `.glsl` ships its `.metal` twin in the same PR, and
  the new Metal compute kernel **must** be added to `threadgroupSizeForFunctionName` in
  `metal_pipeline.cpp` or it defaults to a 1x1x1 (top-left-corner-only) dispatch with no error.
- **Confirm the composite's indirect draw path early** (gotcha 1). Indirect *dispatch* is proven
  in `system_voxel_to_trixel.hpp`; if `drawElementsInstancedIndirect` is not yet exposed on the
  device for both backends, either add it (small, symmetric to the existing indirect-dispatch
  wrapper) or drive the instance count from an SSBO-read count -- decide at first contact, do not
  block the compaction work on it.
- **Per-frame reset + barriers.** The append counter and indirect-arg buffer must be reset each
  frame before the scan, with a `memoryBarrier` between scan->draw and scan->resolve-dispatch.
  Mirror STAGE_1's existing reset discipline.
- **Sentinel + sub-cell bits.** The store packs depth with frac bits (#1458: `rawDist>>10`); the
  non-empty test compares against the sentinel `kTrixelDistanceMaxDistance`, not 0 -- reuse the
  resolve/store's existing convention exactly.
- **Don't touch route 0.** Cardinal must remain the byte-identical single-canvas gather; the
  compaction path is taken only while `perAxisCanvases_->isAllocated()` (rotating).
- **Sibling reconciliation:** #1937 (epic #1933, "analytic edge-aware coverage on per-axis scatter
  -- GL backend") also edits the per-axis scatter shaders, but for *coverage/AA*, not cell sourcing
  -- largely orthogonal (per-fragment coverage vs which cells become instances). If both land
  near-simultaneously they touch `*_peraxis_scatter.*`; whoever lands second rebases the cell-source
  vs coverage hunks (no semantic conflict expected). #1958 (depth invariant) and #1957 (detached
  composite depth) are **merged**; #1959 (cardinal-180 tiebreak) and #1960/PR #1989 (per-trixel
  priority demos, design-blocked) are the other #1884 per-axis family members -- none change the
  per-axis cell-instancing surface this plan owns.
- **One PR.** Cohesive: one shared pre-pass, two consumers. If it genuinely exceeds one PR during
  implementation, the natural split is (a) compaction pre-pass + composite indirect draw, then
  (b) resolve indirect dispatch consuming the same list -- but do not pre-split; escalate via the
  worker scope-grew path only if it actually overflows.

