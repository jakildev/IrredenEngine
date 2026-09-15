# Voxel cull work units

The compact diagnostics distinguish unique source candidates from repeated axis
list entries. A voxel appended to all three face-local lists contributes one to
`Visible` and three to `AxisEntries`. The shader reduces unique contributions
within each 64-lane workgroup and adds one group sum to indirect word 5; the
32-byte dispatch structure and its aligned region stride are unchanged.

`Total` is the producing dispatch's pool-slot domain, including inactive slots.
For inverse revoxelization this is the destination-slot domain, not authored
source voxels. `Visible` counts unique main-list candidates, not pixels actually
visible after depth testing. `Feeder` counts the separate cardinal shadow list;
per-axis rendering has no separate feeder list. `Ratio` is
`(Visible + Feeder) / Total`, so both routes describe unique retained candidates.
It measures combined active/exposed/domain/occlusion rejection, not Hi-Z alone.

The indirect buffers are shared between canvases. Readback uses the previous
**dispatch's** route and pool size, captured after submission, rather than the
current canvas or camera state. Each result is consumed before buffer reset.
First use and unprofiled dispatches are excluded. Empty-pool early returns do not
replace the previous producer metadata. Samples remain per-dispatch and the last
pending dispatch is not drained at shutdown. Readback may synchronize with the
GPU, especially between canvases; it is not an asynchronous profiling pipeline.

Old reports used axis entries as rotated `Visible` and excluded separate feeders
from `Ratio`. The comparator detects the new `AxisEntries` row and refuses a
cross-version cull delta. Timing rows remain independently comparable under their
usual measurement constraints.

## Validation

Native M4 Max Metal Debug; full commands, hashes, reports and summaries are in
[voxel-cull-work-units/](voxel-cull-work-units/).

- Static 32³ solid at yaw45°, FULL/base1, zoom2: maximum unique boundary count
  5,768 = 32³ − 30³; axis entries 6,144 = 6 × 32².
- Static 17³ solid: maximum unique count 1,538 = 17³ − 15³; axis entries
  1,734 = 6 × 17². Its 4,913 slots exercise a partial final workgroup.
- Startup samples lower the averages; the exact steady-state maxima above are
  the analytical checks. The displayed scene cannot prove those counts alone.
- Frozen grid16, FULL/base1, zoom4: 15 wide-angle/cardinal poses have identical
  scene RGB before/after. Compare x<1800 over the full 1440-pixel height of the
  2560×1440 captures, excluding the changing overlay. Captures 57–71 precede this
  slice and 175–189 follow it. [Full representative captures and pair results](../pr-screenshots/codex/voxel-cull-work-units/).
- IRCanvasStress default multi-canvas scene, nine poses from yaw0 to 2π, no
  self-spin: clean exit with the producing-pool survivor guard enabled.
- Deliberately tripling the staged Metal unique contribution trips the guard
  `Unique compact survivors exceed the producing pool slot count`. The staged
  shader was restored through `fleet-build` before the successful final 17³ run.
- Five parser/comparator tests pass; disabling mixed-unit detection makes its
  regression test fail. Native builds, format, header/Metal registry checks,
  comment-reference lint and ruff pass. OpenGL runtime remains unverified.

The animated 64³ yaw45°/zoom4 exploratory runs average roughly 259K retained
sources and 777K axis entries. Compact timing averages 0.346 ms (0.332–0.357 ms),
versus 0.323 ms in the earlier dispatch-density measurements. Frame mean is
22.457 ms (21.900–22.990 ms). These runs overlap a demo build and are **not** an
isolated overhead experiment; no speedup or zero-overhead claim follows.

An initial culling-toggle comparison uses frozen grid64, yaw0, zoom2, NONE mode,
no sun shadows, 120 frames per run. Average retained candidates fall from
259,941 to 32,437 with `--occlusion-cull`; frame means are 9.080 and 8.970 ms.
This establishes active rejection rather than a vacuous toggle, but one short
run per variant does not establish a meaningful frame-time improvement. The
existing Hi-Z gate excludes residual yaw and subdivision modes; extending that
gate requires a new projection/coverage proof, not merely removing its guard.

## Remaining work

This completes the unique-candidate/axis-unit and producer-attribution portion
of the [counter TODO](rotation-subdivision-audit.md#proposed-optimization-todo).
Logical subdivision slices, occupied cells, overflow entries and scratch bytes
remain. Separate visible-domain work from shadow-domain work before interpreting
rotation/zoom retention ratios as screen-efficiency measurements. Establish
alternating, uncontended profiling controls and deferred readback overhead before
optimizing from small timing differences.
