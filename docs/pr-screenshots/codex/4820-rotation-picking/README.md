# Rotation picking WIP evidence

Control: `795e16adb809e526536e734e9b56b4bbe60a170f`, fixture-only on
`8a38400646e46b3d3e72adc2b56dfacf9e4c85c6`. Engine implementation:
`bdbe31ea0f2387cede5e123490c73f4e94fdb1e2`. This is an incomplete design
handoff for #4820, not acceptance evidence for a merge-ready change.

Host: macOS, Apple M4 Max, Metal, macos-debug. Scene: one yellow voxel at
(-40,-40,0), isolated from the ordinary ShapeDebug scene, no lights or shadows.
Zoom 16; subdivisions NONE (effective 1); game resolution 1280×720; captured
output 2560×1440. The eight yaws are 20, 0, 70, 90, 160, 180, 250, 270 degrees.
The camera follows the top-face aim; exact per-shot camera values and hover
assertions are in `control-gui.txt` and `head-gui.txt`.

Both captures used `fleet-jobs start gui-verify -- IRShapeDebug --no-build --
--rotation-pick-test gui`, which runs `fleet-run --timeout 120 IRShapeDebug
--auto-screenshot 10 --rotation-pick-test gui`. The control was built and
captured from the fixture-only commit. The final head capture ran after the
implementation commit. Both jobs were waited to completion: control job
`20261102T030536Z-gui-verify-3ee04c` failed with four rotating misses and four
cardinal passes; head job `20261102T032545Z-gui-verify-2353fa` succeeded, 8/8.

The full before/after frames are pixel-identical (decoded RGB comparison,
zero changed pixels for each pair). Hover output changes from zero to the
voxel's ID on the four rotating shots. The 320×320 detail images are exact
crops at (1120,560)–(1440,880); they and the full frames were inspected.
No clip was recorded for this WIP handoff. Picking is asserted in logs and
does not change colour or enable scatter hover highlighting.

`probe-results.txt` preserves selected native output. Head sweep passes
360/360; cube passes 288/288. Baseline sweep has 356 misses, and baseline cube
has 288 misses. The tie probe is deliberately still failing: all 15,372 reads
are allowed IDs, but A never appears at 160° or 250° inside B's bounding box.
At 160°, A's projected vertical displacement is exactly one projected cube
height, so the bounding boxes have no interior overlap. The required A
presence at every yaw cannot be accepted with the locked fixture geometry.

The tie run was executed once on head; three-run repeatability is unverified.
Other unfinished work: retain main-pool IDs across uploads by other canvases,
bound four-word scratch indices including the prefix, full final acceptance
suite, native GL validation/follow-up, and interleaved performance grading.

The pre-publication lighting job `20261102T032057Z-light-verify-f12cb5`
completed successfully with all checks PASS. Default and continuous-zoom
ShapeDebug sessions passed 45 and 5 assertions, respectively. Their selected
output is included here. The full hermetic run initially passed 118/124 suites;
the six affected adapters were corrected and individually rerun successfully.
A complete final hermetic invocation remains required before leaving WIP.

Build job `20261102T032042Z-build-488c27` completed successfully (exit 0)
for ShapeDebug, CanvasStress, VoxelEditor and IrredenEngineTest. Native
overflow-sort tests (2/2) and the framebuffer attachment test (1/1) pass;
see `native-tests.txt`, including the expected pre-allocation assertions.
