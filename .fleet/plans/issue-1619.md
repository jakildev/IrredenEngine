# Plan: re-voxelize round-to-cell coverage holes (missing faces) (#1619)

- **Issue:** #1619   **Model:** opus   **Date:** 2026-06-09 (architect plan)
- **Status:** ROOT CAUSE CONFIRMED BY REPRODUCTION (architect built + ran IRCanvasStress
  --auto-screenshot on macOS/Metal, 2026-06-09). The defect is visible on every
  DETACHED_REVOXELIZE solid; clean solid orbit shapes of the same kind (GRID / plain-DETACHED)
  next to them prove it is mode-specific. The `visibleTriplet` sign-flip theory is RULED OUT.

## Symptom (confirmed)
Banded / speckled coverage holes on DETACHED_REVOXELIZE solids — rows of cells round to the same
line leaving gaps between rows. Severity scales INVERSELY with solid size: large solids show
"patterns of gaps," small canary cubes lose what reads as a whole face (the gaps span it). NOT a
`visibleTriplet`/face-selection bug (re-voxelize uses a FIXED cardinal triplet,
`voxel_frame_data.hpp:85`, not `visibleTriplet`).

## Root cause
`engine/render/src/shaders/c_revoxelize_detached.glsl` (+ Metal twin + CPU mirror
`IRPrefab::GridRotation::worldCellForGridVoxel`) is a **forward scatter**: one thread per *source*
voxel writes `cell = roundHalfUp(rotateByQuat(local, quat))`. Forward rotation+round is **not
surjective** onto the rotated-AABB lattice -> covered dest cells get no source voxel -> holes. The
2D +/-1px raster dilation only closes sub-pixel seams, not whole missing cells. Shared with attached
GRID re-voxelize (`REBUILD_GRID_VOXELS`) — same model; large GRID cubes hide it.

## Approach (instrument-first — this surface mis-diagnosed twice already, cf. #1457 + the visibleTriplet red herring)
1. Instrument: dump filled-cell set vs rotated-AABB lattice; classify holes surface-vs-interior;
   confirm scaling with angle + inverse with size (the repro already shows the inverse-size scaling).
2. Lead — INVERSE (backward) resampling: dispatch over DEST cells in the rotated AABB; fill `c` iff
   `roundHalfUp(rotateByQuat(c, conj(quat)))` is in source occupancy. Surjective -> hole-free.
   Needs a source-occupancy structure (hash/bitset), a matching CPU mirror (keep CPU<->GPU
   bit-identity), and a dest-cell dispatch domain (the pool already sizes to the rotated AABB).
3. Fallback — guaranteed 3D dilation in CELL space (close 1-cell gaps). Cheaper, approximate.

## Constraints
- CPU<->GPU bit-identity; GLSL<->Metal parity in lockstep; identity-rotation fast-path byte-identical.
- Cover BOTH DETACHED_REVOXELIZE and GRID re-voxelize.

## Definition of done
- DETACHED_REVOXELIZE solids of all sizes render hole-free across a full spin in canvas_stress
  (verify the center cluster + the gappy orbit shapes from the repro: blue/yellow/teal/green/red).
- Small (<=4^3) canary cube: no whole-face dropout.
- Re-enable a thin shape (frame/cross) on the re-voxelize path in canvas_stress as the standing guard.
- GLSL+Metal parity; render-verify ROI crops committed.

## Ownership
`[opus]`. Correctness follow-up to the #1553 re-voxelize epic (coverage track; distinct from the
P4b lighting track and from #1620 the floor-depth demo fix).

---

## Architect decision (design-unblock, 2026-06-09) — PR #1623

Worker instrumented and surfaced the binding constraint: the raster reads position (b5),
color+flags (b6), active (b8) all keyed by the SAME source-voxel slot, on bindings shared with the
main-canvas raster; inverse resampling is dest-cell-indexed with count > source count. Decision:

**Go with Option A (resize + resample-write). Reject B/C/D.**

The win: A keeps the slot-indexed contract byte-identical and only **redefines what slot `i` holds**
— "dest cell `i`" instead of "source voxel `i`". The shared compact/stage1/stage2 raster is
**UNTOUCHED** (it still reads position[i]/color[i]/active[i] and rasterizes active slots). The entire
change confines to the **fill** (`c_revoxelize_detached.{glsl,metal}`) + pool sizing + a source
lookup. That's why A beats B (B branches the SHARED raster → main-canvas byte-identity + Metal
parity risk). C (dilation) and D (supersampling) are approximate — they don't "solve it once and
for all" (the user's explicit goal), so not the primary; keep C only as a fallback if A's sizing
proves prohibitive.

### Answers to the worker's three questions
1. **Colors:** the fill authors dest-cell color per frame into the re-voxelize pool's OWN ColorBuffer
   (`color[i] = srcColor(inverse(c_i))`). This is re-voxelize-only — the main canvas still seeds its
   ColorBuffer once and is unchanged. Add a source-color lookup alongside occupancy (pack into
   `residentLocals.w` or a parallel resident-colors buffer). stage1/2 just read `color[i]` — no
   shared-shader change. Same for the active bit: the fill sets per-slot active so empty dest cells
   (inverse miss) are inactive, in whatever representation the compact pass already consumes.
2. **Pool sizing:** size to a **closed-form bound = occupied volume + one-cell boundary shell**
   (≈ `N³ + C·N²`, C≈6), NOT the loose rotated-AABB cube (~5×) and NOT the source count. Rotation is
   volume-preserving; only the discretization boundary grows. Derive the exact bound CPU-side
   (opus-tier closed form). If the closed form proves fiddly, fall back to the AABB cell count
   (accept the memory) — never a hard cap that silently drops cells (that reintroduces holes).
3. **Shared bindings:** stay ON the shared contract via dest-cell slot semantics (Option A). Do NOT
   add a re-voxelize branch to the shared raster shaders.

### Constraints (unchanged, re-emphasized)
- **CPU mirror** must replicate the inverse resampling bit-identically (`roundHalfUp` on `inverse(c)`).
- **Identity rotation fast-path byte-identical**: at identity `inverse(c)=c` → dest cells = source
  cells, color = seeded color, positions = source. Short-circuit to today's behavior.
- GLSL↔Metal parity in lockstep. DoD unchanged (hole-free all sizes; no whole-face dropout on small
  cubes; thin-shape guard re-enabled; render-verify ROI).

Resume on PR #1623 (claim commit + this thread). Any opus-worker can pick up `fleet:design-unblocked`.

---

## Architect decision #2 (design-unblock, 2026-06-09) — PR #1623: rotated solids drop out entirely

Option A's inverse resample is implemented and reviewed, but the human confirmed the artifact is
NOT resolved: rotated DETACHED_REVOXELIZE solids show missing faces — and the worker's repro shows
they in fact **render nothing at all** at non-identity rotation (non-deterministically: one smoke
pass green, the next all-gone). Worker hypothesis: all ~11 re-voxelize canvases run the mode-1 fill
into the SHARED single-voxel SSBOs (bindings 5/6/8); mode 1 GPU-authors position+color+active and
needs them to survive until that canvas's own compact/raster, but across canvases they clobber.
Mode 0 survives only because the CPU re-uploads color+active per canvas.

### The goal — restated as the ONLY definition of finished

**Every DETACHED_REVOXELIZE solid renders, hole-free, at every rotation pose and every size —
including the small canary cubes (no missing faces) and the two center proof solids (no missing
solids).** Code that builds clean and passes review but still shows the artifact is not done.

### Direction

1. **Step 0 (mandatory, cheap, decisive): single-canvas isolation repro.** Run a scene with exactly
   ONE rotated DETACHED_REVOXELIZE solid. Renders correctly alone but breaks with neighbors →
   cross-canvas clobber CONFIRMED. Breaks alone too → the bug is inside one canvas's own mode-1
   chain (fill/pre-clear/compact/raster ordering) and private buffers alone will NOT cure it —
   follow that evidence instead. Post the result on the PR either way before building the fix.
   (This surface has burned two reviewed-and-approved no-fixes already — evidence before code.)
2. **Structural fix (assuming clobber confirms): per-pool PRIVATE dest-domain buffer set** —
   position / color / active-mask / chunk-vis / compacted — sized per pool to the dest domain
   (closed-form bound from decision #1; rotated-AABB cell count is an acceptable fallback). This is
   what "private pool" in decision #1 meant. It fixes the clobber by construction AND retires the
   `IR_ASSERT(destCount <= maxSingleVoxels)` cap, which contradicts "hole-free at every size".
   Mode 0 (identity) stays on the shared buffers untouched — byte-identity preserved. Mode 1 binds
   the pool's private set at the same binding points; slot semantics unchanged; no shared-shader
   branch.
   - **Invariant (engine-level):** a re-voxelize canvas's GPU-authored fill output must be
     guaranteed intact at that canvas's own compact/raster. Never alias GPU-authored state across
     canvases through shared SSBOs unless the command stream provably serializes fill→raster per
     canvas (if you take that route instead — single shared scratch sized to max dest-count —
     the serialization must be demonstrated with barriers in the command stream, not assumed).
3. **Un-bless the regressed reference.** This PR re-blessed `references/macos-debug/
   revoxelize_solids.png` to the solids-missing state — render-verify would green-light the
   regression. Restore from master; re-bless only from a verified-good build.
4. **Pose-deterministic evidence.** The contradictory smoke verdicts came from canvas_stress pose
   non-determinism. Canonical evidence = `--no-spin` shots (proof solids hold `initialRotation`,
   a fixed rotated pose). Spin shots are supplementary only.

### Definition of done (supersedes prior DoD where they differ)
- Single-canvas isolation result posted (step 0).
- All DETACHED_REVOXELIZE solids present + hole-free in `canvas_stress --no-spin` (proof solids,
  canaries, orbit re-voxelize shapes) AND under spin, on macOS/Metal and Linux/GL.
- Identity fast-path byte-identical; GRID re-voxelize and main canvas unaffected.
- `IR_ASSERT(destCount <= maxSingleVoxels)` retired (private sizing) — no silent or loud size cap
  on the dest domain.
- References re-blessed from a verified-good build only; before/after screenshots in the PR.
- GLSL + Metal in lockstep.

Resume on PR #1623. Any heavy-tier worker can pick up from `fleet:design-unblocked`.
