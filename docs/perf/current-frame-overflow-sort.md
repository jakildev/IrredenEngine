# Current-frame overflow sort

Sort eligibility still uses the pool's displaced-cell collision flag. GPU work
derives from the current append count. A preparation mode writes indirect grids
for fill, local sorting and stages 12..30. Empty lists and stages beyond the
active span produce zero-sized grids. CPU encoding uses a same-frame upper bound
of three records per voxel slot in admitted chunks, clamped to the live voxel
count and lane capacity. An empty flagged pool therefore opens three encoders
rather than the capacity-sized chain, while any current-frame population growth
still encodes every merge stage its list can require. Completed-frame readback is
diagnostic only and every frame is fully sorted before the lane is consumed.

The scratch control region grows from 256 to 512 bytes. Its first eight words
retain draw/counter ownership; 21 four-word commands fit afterward. Storage and
command barriers order argument production and consumption. Both backends flatten
2-D workgroup grids, preserving coverage beyond the X dispatch limit. Rendering
no longer uses the diagnostic readback as a correctness input.

## Validation

The focused GPU test covers empty input, a first population within the local
block, growth from zero, a tripled population, shrinking counts,
draw/counter preservation and a fill dispatch spilling into Y. Every case
matches a CPU lexicographic reference. A unit test covers the admitted-slot,
live-count and lane-capacity clamps.
IRPerfGrid, IRCanvasStress and IrredenEngineTest built; header/Metal registry,
comment and diff checks passed. Review found no correctness blockers.

All nine full-turn CanvasStress capture pairs are full-RGB identical;
[results](current-frame-overflow-sort/canvas-comparison.json) retained. OpenGL
runtime validation remains outstanding. Unflagged cross-cell band-code ties
remain outside this sort's eligibility contract.

| Parent | Current-frame sort (identical RGB) |
|---|---|
| ![Before](https://raw.githubusercontent.com/jakildev/IrredenEngine/d99aba4ccd648fd3c336b5b55fef85dbb8a8aceb/docs/pr-screenshots/codex/current-frame-overflow-sort/capture-1224.png) | ![After](https://raw.githubusercontent.com/jakildev/IrredenEngine/d99aba4ccd648fd3c336b5b55fef85dbb8a8aceb/docs/pr-screenshots/codex/current-frame-overflow-sort/capture-1233.png) |

## Measurements

Apple M4 Max, Metal Debug, default lights/shadows, voxel-set frozen wave, yaw45°,
zoom4, GPU profiling on; two 300-frame runs each including initial frames.
Million uses 100³ entities/pool128; default and empty use 64³ entities/pool64.
Empty sets wave amplitude0; other cases retain amplitude5. Cases ran in table
order. The runtime Lua files match the earlier capacity audit's
[pool128](million-entity-capacity/pool128-config.lua) and
[pool64](million-entity-capacity/pool64-config.lua) snapshots. Runtime config restored.

| Case | Frame mean ms (run range) | Overflow GPU invocation mean ms |
|---|---:|---:|
| million | 52.340 (52.120–52.560) | 9.829 |
| default | 17.705 (17.700–17.710) | 2.669 |
| empty | 8.710 (8.640–8.780) | 0.068 |

An interleaved parent/head rerun was attempted during feedback, but the
unattended macOS session reported no display monitors before creating a frame.
Those launches produced no profile report and are excluded from this table.

Reports and binary/shader fingerprints are in
[current-frame-overflow-sort/](current-frame-overflow-sort/). No overflow-drop
diagnostics appeared. The million frame mean remains in the prior live-lighting
run range; no controlled frame-time speedup is claimed. The sampled overflow
invocation is lower than the earlier approximately10.8ms samples, but those
samples are not an interleaved control. Do not sum invocation rows into a GPU
frame budget. Full-frame GPU accounting is the next profiling task.

The existing retired-frame diagnostic read is a shared-memory memcpy on Metal.
GL may synchronize; true nonblocking diagnostics need a separate snapshot/fence
API, not removal of warning coverage or a ring that maps buffers without fences.
