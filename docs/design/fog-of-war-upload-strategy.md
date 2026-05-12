# Fog-of-war CPU→GPU upload strategy

**Status:** Deferred. The current full-buffer dirty-gated `subImage2D`
upload remains the implementation, and `C_CanvasFogOfWar::dirty_` /
`allUnexplored_` remain the documented exception to the no-dirty-flags
rule (see `.claude/rules/cpp-ecs.md` §"No dirty flags on components").
T-161 evaluated migration to per-write `subData` semantics (the
follow-up that PR #638 originally suggested) and concluded the
optimization does not pay under any workload exercised by today's
engine.

This doc captures the strategy comparison, the workload reality that
gates the call, and the migration sketch for the day the trigger
actually fires — so the next agent doesn't have to re-derive the
analysis.

**Problem owner:** rendering —
`engine/prefabs/irreden/render/components/component_canvas_fog_of_war.hpp`
and `engine/prefabs/irreden/render/systems/system_fog_to_trixel.hpp`.

**Originating review:** PR #638 Opus recheck (nit N3). The reviewer at
the time cited `.claude/rules/cpp-ecs.md` "No dirty flags on
components"; that rule landed later in PR #651 alongside the
C_GPUParticlePool deferred-flush variant. The rule now lists fog-of-war
as the single allowed exception, with this doc as the deferral
rationale.

---

## The texture and the access pattern

`C_CanvasFogOfWar` owns a 256×256 RGBA8 texture on the iso ground plane
(X-Y axes; +Z is height). Only the `.r` channel carries fog state
(`unexplored` / `explored` / `visible`); the other three channels exist
only because the Metal backend's rgba8 image binding path shares a
binding layout with the AO and sun-shadow textures.

- CPU mirror: `std::vector<std::uint8_t> cpuBuffer_` of size
  256 × 256 = 64 KiB (.r channel only).
- GPU texture: 256 × 256 × 4 = 256 KiB.
- Access shape: CPU is the sole author. GPU is read-only —
  `c_fog_to_trixel.glsl` / `.metal` samples it from the
  `LIGHTING_TO_TRIXEL` follow-on stage. No GPU writeback.

Mutators on the CPU side:

- `setCell(wx, wy, state)` — single cell, taxicab-bounded.
- `revealRadius(cx, cy, radius)` — taxicab disk fill,
  `O((2r+1)²)` cell writes worst case.
- `clearAll()` — `std::fill` over the whole buffer.

Each mutator sets `dirty_ = true`; the `FOG_TO_TRIXEL::tick` reads the
flag, expands the .r-only `cpuBuffer_` into a transient RGBA8 scratch
(`uploadScratch_`, owned by the system params so the per-frame upload
is allocation-free), uploads via a single `subImage2D` over the full
texture, and clears the flag.

A second flag — `allUnexplored_` — short-circuits `clearAll()` when the
buffer is already empty, avoiding both the `std::fill` and the upload.

---

## Three candidate strategies

### Strategy A — Current: full-buffer subImage2D on dirty

Today's implementation.

- **Calls per dirty frame:** 1 `subImage2D` covering the full 256×256
  rect.
- **CPU work per dirty frame:** 65,536 byte writes into
  `uploadScratch_` (the `.r → RGBA8` expansion loop).
- **GPU bytes per dirty frame:** 256 KiB.
- **Driver overhead:** one command, regardless of how many cells
  actually changed.
- **Call count per non-dirty frame:** 0 (early-return on `!dirty_`).
- **Worst case:** dominated by buffer size, not mutation rate.

### Strategy B — Per-write subData

The reviewer's original suggestion: drop the dirty flag and have each
mutator issue its own `subImage2D` covering the touched cell(s).

- **`setCell` cost:** 1 `subImage2D` per call. Reasonable for sparse
  point updates.
- **`revealRadius(r)` cost:** up to `(2r+1)²` `subImage2D` calls if
  emitted per cell — 2,401 calls at r=24 (the lua_perf_grid /
  perf_grid radius is 128, which would clip to the 256×256 buffer
  and still emit thousands of calls).
- **Per-call overhead on Metal:** each `replaceRegion` synchronizes
  the texture. Per-call cost is dominated by command-encoder
  overhead, not by the byte payload.
- **Verdict:** strictly worse than Strategy A for the
  `revealRadius`-dominated workloads today. Strictly better than
  Strategy A only when the engine sees true sparse single-cell
  updates at high frequency without bulk reveals — a workload that
  does not exist in any current creation.

### Strategy C — Per-region (dirty-bbox) subImage2D

A generalization: track the bounding rect of touched cells per
dirty frame; upload only that rect.

- **Component state:** four ints (`dirtyMinX_`, `dirtyMaxX_`,
  `dirtyMinY_`, `dirtyMaxY_`) plus the existing `dirty_` /
  `allUnexplored_` flags. The bbox tracks per mutator
  (`setCell` expands by one cell; `revealRadius` expands by the
  clamped reveal rect; `clearAll` expands to full).
- **System work:** size `uploadScratch_` to `bboxW * bboxH * 4`,
  copy `.r` rows from `cpuBuffer_` into the rect-shaped scratch
  (one `memcpy` per row would be wrong since stride differs;
  per-cell expansion is still 1 byte → 4 bytes), call
  `subImage2D(bboxMinX, bboxMinY, bboxW, bboxH, ...)`.
- **Calls per dirty frame:** still 1 — bbox is a union, not a
  partition. Disjoint updates collapse into a single conservative
  rect.
- **Driver overhead:** identical to Strategy A (one command per
  dirty frame).
- **Bytes per dirty frame:** `bboxW * bboxH * 4`, ranging from
  16 B (single `setCell`) through `(2r+1)² * 4` (a single
  `revealRadius`) to the full 256 KiB (`clearAll` or two
  far-apart reveals whose bbox spans the buffer).
- **Verdict:** strictly dominates Strategy A in best/average
  case, ties Strategy A in worst case. Strictly dominates
  Strategy B in call count.

Both backends already support arbitrary-rect uploads with a
tightly-packed source pointer — OpenGL via `glTextureSubImage2D`
(unpack-row-length defaults to tight), Metal via
`replaceRegion` with `bytesPerRow = width * pixelSize`. No backend
work is needed beyond what's there today.

---

## Workload reality today

Across the four creations that currently exercise `C_CanvasFogOfWar`,
**every call site is a one-shot `revealRadius` at init**:

- `creations/demos/shape_debug/main.cpp:501` — `revealRadius(0, 0, 48)`
- `creations/demos/perf_grid/main.cpp:329` — `revealRadius(0, 0, 128)`
- `creations/demos/lua_perf_grid/main_lua.cpp:275` — `revealRadius(0, 0, 128)`
- `creations/demos/lighting/common/lighting_demo_scene.hpp:328` — `revealRadius(24, 6, 42)`

The system tick still fires every frame in the RENDER pipeline, but
the `!dirty_` early-return short-circuits to zero work after the first
post-init frame. **Steady-state per-frame upload cost is identically
zero across every current consumer.** The total upload cost across an
entire session is two uploads (the ctor's initial-state push + the
init-time `revealRadius`), each ~256 KiB, both during scene setup.

A 256 KiB texture upload during scene init is not a hotspot on any
backend by any reasonable definition. On modern desktop GPUs:

- Sustained PCIe bandwidth: ≥ 16 GiB/s.
- Sustained memory bandwidth: ≥ 100 GiB/s.
- Initial-state upload cost: ~256 KiB / 16 GiB/s ≈ 16 microseconds
  best case; on Metal the orphan-then-copy semantics on an
  RGBA8 texture this size are still well under a millisecond
  per upload.

Even if a future consumer reveals the radius once per frame at
60 Hz with no caching, the sustained bandwidth (256 KiB × 60 ≈
15 MiB/s) is well below 0.02% of available bandwidth on either
backend.

---

## The migration trigger

Strategy C becomes worth implementing when at least one of these
fires:

1. **High-frequency per-frame mutation.** A consumer that calls
   `setCell` or `revealRadius` ≥ 10× per frame and dirties the buffer
   most frames. RTS-style multi-source LOS, real-time fog-of-war
   updates from moving units, or per-frame line-of-sight ray casts
   would all qualify.
2. **Sub-frame upload contention.** A future feature that reads the
   fog texture from a CPU-side callback in the same frame it was
   mutated — at which point the orphan-the-whole-texture semantics
   of Strategy A become a synchronization stall the bbox shape
   eliminates.
3. **Larger texture extent.** A scope expansion to 1024² or 4096²
   would push per-dirty-frame upload past 4 MiB / 64 MiB, at which
   point the size-proportional cost actually shows up in frame-time
   profiles.

None of these are true today. T-161 documents the trigger so the
next observer who sees one of them fire can pull this doc up, run
the migration in section "Strategy C migration sketch", and ship the
change with measured numbers.

---

## Measurement plan (deferred)

The acceptance criterion "per-frame fog upload cost (CPU + GPU) under
three workloads" calls for measurement against a workload generator
that does not exist. Building one is a separate task:

- A `fog_stress` demo (or a `--fog-stress` flag on `shape_debug`)
  that issues each of the three workloads (single `setCell` per
  frame; `revealRadius(0, 0, 32)` per frame; `clearAll` per
  frame) for 1000 frames with `IR_PROFILE_FUNCTION` markers on
  `FOG_TO_TRIXEL::tick`.
- `fleet-run …  --auto-screenshot N --enable-profiler` to capture
  per-frame timing into the profile report.
- Comparison rows for Strategy A vs Strategy C, on both OpenGL
  (Linux) and Metal (macOS).

Filed as the natural follow-up to T-161 when one of the trigger
conditions actually emerges. Doing the measurement before a real
workload exists would produce numbers that do not reflect any
shipped consumer — fabricated benchmarks are worse than no
benchmark.

---

## Strategy C migration sketch

When the trigger fires, the migration is mechanical. The change is
contained to two files; the existing `dirty_` flag is preserved as
the gate (Strategy C narrows the rect, it doesn't remove the gate).

**`component_canvas_fog_of_war.hpp`:**

1. Add four `int16_t` (or `int`) fields:
   `dirtyMinX_`, `dirtyMaxX_`, `dirtyMinY_`, `dirtyMaxY_`. Default
   to the full buffer (`-kFogOfWarHalfExtent` /
   `kFogOfWarHalfExtent - 1` on each axis) so the ctor's initial
   upload still pushes the all-zero baseline.
2. Add a `markDirtyCell(int wx, int wy)` private helper that
   updates `dirty_ = true` and expands the bbox to include
   `(wx, wy)`.
3. Add `markDirtyRect(int xMin, int yMin, int xMax, int yMax)` for
   `revealRadius` and `clearAll` (which dirties the full buffer).
4. In `setCell`: replace the inline `dirty_ = true` with
   `markDirtyCell(wx, wy)` on the change path.
5. In `revealRadius`: precompute the clamped reveal rect (already
   available as `xMin`/`yMin`/`xMax`/`yMax`) and call
   `markDirtyRect` after the loop, on the path where any cell was
   actually mutated. The current per-cell `dirty_ = true` inside
   the loop can be dropped.
6. In `clearAll`: call `markDirtyRect` with the full extent.
7. Add a public `consumeDirtyBbox()` that returns the current rect
   and resets `dirty_ = false` plus the bbox back to "empty"
   (sentinel state: min > max).

**`system_fog_to_trixel.hpp`:**

1. Replace the `if (fog.dirty_)` block with
   `auto bbox = fog.consumeDirtyBbox();` plus an early-return on
   "empty" bbox.
2. Compute `bboxW = bbox.xMax - bbox.xMin + 1` and `bboxH`
   analogously.
3. Size `uploadScratch_` to `bboxW * bboxH * 4`; the
   value-initialization on resize keeps the GBA bytes at zero.
4. Fill the scratch row-by-row from `cpuBuffer_[flatIndex(bbox.xMin
   + dx, bbox.yMin + dy)]` for each `(dx, dy)` in the bbox; only
   write the `.r` byte at stride 4.
5. Call `subImage2D(bbox.xMin + kFogOfWarHalfExtent, bbox.yMin +
   kFogOfWarHalfExtent, bboxW, bboxH, …)` — the texture coordinate
   space is `[0, kFogOfWarSize)`, so each bbox component shifts by
   `+kFogOfWarHalfExtent` to map from world-space cell coords to
   texture-space pixel coords.

**Validation after migration:**

1. `fleet-build --target IRShapeDebug` and `--target IRGameDemo`
   (whichever non-render-stripped demo is current).
2. `fleet-run … --auto-screenshot N` on the four existing
   consumers; before/after image diff should be all-zero (no
   visual change).
3. Run the deferred `fog_stress` measurement plan (above) to
   confirm the expected improvement in per-frame CPU + GPU
   timing.
4. Update `.claude/rules/cpp-ecs.md` Live deviations entry:
   either drop the entry entirely (if Strategy C eliminates the
   need for the dirty flag — it does not, since the gate still
   exists) or update the entry's description to say "per-region
   `subImage2D`" instead of "full-texture upload".

The migration touches ~60 lines of code, preserves all current
behavior (visual output, ECS surface, naming), and is reviewable
as a single PR.

---

## Cross-references

- `.claude/rules/cpp-ecs.md` §"No dirty flags on components" — the
  rule body. T-161 is the deferral note for the fog-of-war
  exception listed there.
- `engine/prefabs/irreden/render/components/component_canvas_fog_of_war.hpp`
  — component header. Links here from the docstring on
  `dirty_` so a future reader of the field finds the rationale.
- PR #638 — originating review.
- PR #651 — the `C_GPUParticlePool` deferred-flush PR that landed
  the cpp-ecs.md rule body and the canonical per-write `subData`
  example.
