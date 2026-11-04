# Rotation picking evidence

## Native Metal acceptance follow-up (2026-11-04)

[The acceptance receipt](acceptance-2026-11-04.txt) records the checks rerun on
`4e8e10656c23e8c854fd6650cbef9c6205b28fea`, after the corrected implementation
merged in PR #4864. The eight-shot session, 360-degree sweep, three identical
tie runs and 288-sample cube probe pass. Removing only the retained-ID binding
makes the tie probe fail with 5,090 disallowed reads, proving that the hidden
private canvas exercises the binding lifetime.

The macOS display connection worked in this session. Performance acceptance
remains blocked: configuring the archived pre-implementation source attempted
an out-of-sandbox RtAudio package-registry write and received `Operation not
permitted`. [The exact command](blocked-base-configure.json) records its argv.
CMake returned zero with a warning, but the denied operation was not retried
and no baseline build or A/B measurements followed. The manual configuration attempt also
violated the fleet's configured-tree build rule; a resumed run must use a
preconfigured baseline tree. This evidence does not close #4820.
The full ShapeDebug render verifier also fails 19 of 148 checks (ten extra
runs skipped for missing macOS references). Failures are 180/270-degree rows
with maximum channel differences of 109–120 against the limit of 64; the full
table is in [the render report](render-verify-2026-11-04.txt). CanvasStress also
fails 21 of 59 checks; its table is in the same report. No references were
re-blessed and no claim is made that criterion 6 passes. OpenGL runtime
verification remains tracked in #4865.

## Original implementation and amendment history

The captures and readings below describe the first published implementation,
before the feedback amendment. The amendment corrects the cardinal-dependent
tie offset, retains main-pool IDs and bounds four-word scratch addressing.
Its validation and graphics-session limitation at that time are recorded in
`amendment-validation.txt`; the earlier images are retained with their original
commit provenance. The original graphics-session failure was tracked in #4866.

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
