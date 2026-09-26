# Rotation, zoom and finite-shadow-index audit

## Measurement scope

Engine `6521c54dd2c8a383e388b81de745514caa53624c`, Apple M4 Max,
macOS 26.5.2, Metal Debug, AC power. Configured game/window resolution
1280×720; this is not a Release or OpenGL performance qualification.
Two rounds reverse case order. Each run renders 300 frames, excludes the
first 75 from steady wall time, retains CPU system timings when enabled,
and measures completed GPU-frame envelopes independently of stage timers.
The envelope includes stalls and is not GPU busy time. GPU averages cover
startup as well as steady frames; they are not warmup-excluded GPU timings.

The binary, staged shaders and runtime scripts stayed identical within each
matrix. Manifests retain hashes, arguments, host load and pose/overflow
witnesses. Changes to the profiling scripts/tests do not modify the renderer.
Raw reports and manifests are in [shadow-index-audit/](shadow-index-audit/).

## Fixed-scene zoom control

[Eight cases, two rounds](shadow-index-audit/zoom/summary.md) hold a solid
32³ block and pivot fixed while crossing yaw 0°/45°, zoom 1/4 and subdivision
none/full. The overlay is disabled. All sixteen runs complete with 300/300
valid GPU frames; no overflow entries are generated or dropped.

Steady wall means are 8.335–8.410 ms, close to a 120 Hz presentation interval.
This does not prove unrestricted throughput or subdivision parity. GPU
frame envelopes are 3.628–4.010 ms at cardinal yaw and 4.474–4.603 ms at 45°.
The 5,768 retained voxels and 6,144 rotated axis entries do not change across
zooms in this small fixture. It therefore does not stress zoom-dependent
candidate culling or crowded overflow. The million control supplies the
population/wave/overflow stress; a larger fixed-scene zoom matrix remains due.

An initial 180-frame sweep with the profiler overlay enabled was exploratory
and is excluded from the published tables. Both final matrices use 300 frames
and no overlay; full stage timing still includes startup samples.

## Million-entity control

Run `scripts/perf/million_controls.py --rounds 2 --frames 300 --output <fresh-directory>`.
The existing preset uses 100³ single-voxel entities in a 128³ pool, a frozen
per-cell wave, zoom 4, full subdivision/base 1 and a pinned origin pivot.
The six arms cross CPU/GPU-stage profiling on/off with yaw 0°, yaw 45° and
a 1.2°-per-rendered-frame sweep. Overlay is off. Lighting, AO and sun
shadows retain the demo defaults. This is a populated world, not a claim
that one million objects are simultaneously visible.

| Profiling off | Steady wall mean / p95 / p99 ms | GPU envelope mean ms | Max overflow entries / dropped |
|---|---:|---:|---:|
| Cardinal | 33.90 / 36.73 / 40.16 | 22.20 | 0 / 0 |
| 45° | 60.81 / 67.73 / 70.90 | 44.54 | 2,208,000 / 0 |
| Sweep | 42.18 / 46.67 / 49.41 | 28.90 | 971,724 / 0 |

[Full table, ranges and host conditions](shadow-index-audit/million/summary.md).
All twelve runs completed with 300/300 valid GPU frames and no overflow drops.
Profiling-on means are close to profiling-off means; this experiment does not
show profiling overhead as the cause of the diagonal cliff. The sweep advances
358.8° and does not sample an exact 45° frame: its lower overflow maximum must
not be read as having tested the diagonal worst case.

The static diagonal GPU envelope is approximately twice the cardinal envelope.
CPU fixed updates also rise from about 2.0 to 3.7 per rendered frame. The diagonal envelope shows a substantial critical-path cost, but does not
isolate GPU execution from stalls or submission gaps. These results do not
establish a CPU-only route to 16.67 ms.

[Stage rows](shadow-index-audit/million/gpu-summary.md) are sampled invocation
means, not additive or reclaimable budgets. The earlier controlled body
experiments in [GPU cost attribution](gpu-cost-attribution.md) remain the
reason to prioritize overflow sorting and light propagation. This audit does
not establish a regression against those older runs or an optimization win.

## Large finite faces and crowded tiles

The production GLSL and Metal indexers execute in the scalar adapter in
`test_render_source_face_index.py`. A new giant-face fixture covers every tile
in both cascades, first with 64 candidates and then 65. An independently
enumerated list of all 32,768 tile IDs checks record publication, each tile
count and bounded candidate writes, with guard words around the buffer.
Existing mutation controls still reject missing cascades, corrupt records,
off-map quota use and overflowing writes.

Observed contract: each giant face inserts into 32,768 lists; the 65th
coincident face makes every list incomplete. The receiver then consults sampled
fallback depth. This is a deterministic capacity result, not GPU timing or a
visual proof of the fallback. GPU contention/order is outside this adapter.

`indexSourceSunFace` walks the projected face bounding rectangle serially.
`c_bake_box_sun_shadow` invokes it from lane zero of the first workgroup per
box. Its raster fallback already spreads work across lanes/groups; the face
index does not. Bounding rectangles can also consume candidates in tiles
outside the actual projected parallelogram. That increases both insertion
work and the chance of losing exact coverage through overflow.

## Next implementation experiments

1. Use the existing `shapeCastBoxes` GPU substage around the call to
   `bakeAnalyticBoxes` in `SHAPES_TO_TRIXEL`; the voxel-only matrix above
   does not exercise it. Measure large plates and crowded small boxes
   separately before changing scheduling. Add counters for tile insertions,
   incomplete tiles and global face-record exhaustion when isolating index
   pressure from the fallback raster cost.
2. Compare cooperative tile insertion for boxes with the current serial
   loop, retaining one face record and the same complete/incomplete contract.
   Independently test reflected bases, cascade edges, partial overlap and
   global capacity. No receiver blur or enlarged footprint.
3. Measure conservative parallelogram-versus-tile rejection to reduce
   false candidates. It must retain every tile intersected by the actual
   finite face, including boundary hits. It cannot fix genuinely crowded
   overlapping geometry; that needs a measured index-capacity/design decision.
4. Optimize the measured overflow sort/propagation bottleneck and repeat
   the fixed-scene and million matrices with pixel-identity gates. Release,
   longer tail windows, more rounds and native OpenGL remain required before
   a throughput claim.

Open PR #3804 owns the analytical-box rendered-center/self-shadow and cascade
correction. This audit starts from merged master and does not duplicate or
claim to validate that unmerged fix. Fog/procedural/curved receiver coverage
remains a separate visual worklist.
