# Live-count overflow lighting

Overflow lighting consumes indirect dispatch arguments derived on the GPU from
the current frame's settled append count. Unused reserved capacity launches no
lighting threads. The existing cell-finalization pass writes the extra argument
block in unused bytes 64..79 of axis zero's indirect region, so no allocation,
new dispatch, CPU readback, or delayed count is needed.

The append/storage barrier precedes finalization. The finalizer's command barrier
publishes the arguments to lighting. Its temporary control-region binding is
aligned and restored before returning. GLSL and Metal use the same capped 2-D
grid; an empty list produces (1,0,1), and the lighting kernel retains its tail
guard. Allocation capacity and canonical draw order are unchanged.

## Validation

The production GPU finalizer test covers empty/nonempty transitions, a partial
workgroup, the 1024-group X boundary, Y spill, and 8,388,608 entries. It compares
the entire indirect buffer and control buffer, detecting damaged draw arguments,
stale dispatch arguments and unrelated writes. Disabling the Metal argument
writes made the test fail; restoring them passed. Seven focused capacity/GPU
tests passed on Metal, with no skips. OpenGL runtime validation remains pending.

Nine CanvasStress full-rotation poses are full-RGB identical to the parent;
[comparison results](live-overflow-lighting/canvas-comparison.json) retain the
pairs. These include attached, detached and revoxelized entities. The screenshot
comparison is a regression check, not proof that every existing artifact is fixed.

| Parent | Live-count dispatch (identical RGB) |
|---|---|
| ![Before](../pr-screenshots/codex/live-overflow-lighting/capture-1215.png) | ![After](../pr-screenshots/codex/live-overflow-lighting/capture-1224.png) |

## Profiling method

Apple M4 Max, Metal Debug, default lights/shadows, voxel-set frozen wave, 45°
yaw, zoom 4, two 300-frame runs per case including initial frames, GPU profiling
on. Default means 262,144 voxel entities and pool64; million means 1,000,000
voxel entities and pool128. Runtime Lua configurations match the retained
[pool64](million-entity-capacity/pool64-config.lua) and
[pool128](million-entity-capacity/pool128-config.lua) snapshots.

The control keeps this change's finalizer but temporarily restores the parent's
capacity-sized lighting dispatch. It isolates dispatch sizing rather than
comparing unrelated stack revisions. Source variants and binary/shader hashes
are retained with reports. No screenshot, build or other GPU test runs during
profiling. Sampled GPU invocation means are not additive full-frame timings.

Runs occurred in this table's order. The repeated candidate brackets the million
control. Removing the unused C++ group-size constant and correcting shader
comments between candidate blocks changes fingerprints, not dispatch behavior.
The [control patch](live-overflow-lighting/control-dispatch.patch) reconstructs
the dispatch choice; the measured control used the equivalent named constant 64.

| Case | Frame mean ms (run range) | Overflow-lighting GPU invocation mean ms |
|---|---:|---:|
| candidate-million | 53.350 (53.320–53.380) | 13.345 |
| candidate-default | 17.615 (17.610–17.620) | 4.839 |
| control-default | 17.530 (17.500–17.560) | 4.883 |
| control-million | 52.615 (52.300–52.930) | 13.503 |
| candidate-repeat-million | 52.295 (51.920–52.670) | 13.349 |

These results do **not** establish a frame-time speedup: the differences between
candidate blocks are larger than the control/candidate difference. Retain this
as a capacity-independent dispatch improvement with unchanged pictures, not a
headline performance gain. No overflow-drop diagnostics appeared in these runs.
The expensive live-face lighting and other rendering work remain.

Next prioritize current-count sort dispatch (including empty-to-nonempty ordering),
repeated per-axis storage/finalization, and full-frame GPU accounting. Reserved
memory still has the cost recorded in [overflow demand sizing](overflow-demand-capacity.md).
