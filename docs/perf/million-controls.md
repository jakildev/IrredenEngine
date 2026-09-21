# The million controls

The objective's headline fixture
([`million-entity-render`](../design/objectives/million-entity-render.md)) as a
committed recipe: 100³ single-voxel entities in a 128³ pool, FULL subdivision at
base 1, zoom 4, frozen per-cell wave, default lighting and shadows, at yaw 0°
and a true 45°, with stage profiling on and off, in a Debug and a Release tree.

```bash
cmake --preset macos-release      # once; or linux-release / windows-release
fleet-build --target IRPerfGrid
IRREDEN_BUILD_DIR="$PWD/build-release" fleet-build --target IRPerfGrid
python3 scripts/perf/million_controls.py --tree build --tree build-release \
    --output save_files/perf/million-controls
```

## Reference

Apple M4 Max, Metal, macOS 26.5.2, **AC power**. Head `058b495b3` on master
`35b7defc8`, which includes the September shadow stack whose sampler runs inside
this fixture. Three interleaved rounds of 300 frames, 24 runs, every one
`RESULT=CLEAN` with 300 of 300 valid GPU frames. Summaries, reports and
manifests: [million-controls/reference-ac/](million-controls/reference-ac/).
The human ruled a battery run acceptable when recorded (every manifest carries
`host_power`); this one did not need the ruling.

| Build | Stage profiling | Pose | Frame mean ms (round range) | Per-round means ms | GPU frame ms | Updates / frame |
|---|---|---:|---:|---|---:|---:|
| Release | off | 0° | 34.30 (33.79–34.58) | 34.53 / 34.58 / 33.79 | 22.41 | 2.1 |
| Release | off | 45° | 44.60 (43.53–45.52) | 45.52 / 44.74 / 43.53 | 30.41 | 2.7 |
| Release | on | 0° | 34.61 (33.70–35.51) | 34.62 / 33.70 / 35.51 | 22.31 | 2.1 |
| Release | on | 45° | 45.47 (43.74–46.59) | 46.59 / 46.08 / 43.74 | 30.78 | 2.7 |
| Debug | off | 0° | 34.97 (34.77–35.10) | 35.05 / 34.77 / 35.10 | 22.70 | 2.1 |
| Debug | off | 45° | 45.26 (45.01–45.63) | 45.63 / 45.01 / 45.14 | 30.83 | 2.7 |
| Debug | on | 0° | 34.73 (34.00–35.19) | 35.01 / 35.19 / 34.00 | 22.20 | 2.1 |
| Debug | on | 45° | 47.57 (46.39–49.80) | 49.80 / 46.53 / 46.39 | 32.35 | 2.9 |

What it says, and how far each statement goes:

- **The objective's arm (Release, stage profiling off) reads 34.30 ms at 0° and
  44.60 ms at a true 45°**, against 16.67 ms. The GPU frame alone is 22.4 and
  30.4 ms, so no CPU-side change reaches the target by itself, and the CPU runs
  2.1 and 2.7 fixed updates per rendered frame against the objective's 1.
- **Rotated to cardinal is 1.30×** on that arm; the four build and profiling
  pairs read 1.29, 1.30, 1.31 and 1.37×, the last being the Debug
  profiling-on pair that carries the 49.80 ms round. The objective quotes 2.06× at 64³, taken at the
  0.785° pose. This is a different population and a true 45°, so it is a
  measurement of this fixture, not a correction of that row.
- **Release against Debug is 0.7 ms at both poses** with stage profiling off.
  The round ranges overlap at 45° and miss by 0.2 ms at 0°: a difference this
  matrix can see and cannot size.
- **Stage profiling costs nothing measurable at 0° and 0.9 ms (Release) to
  2.3 ms (Debug) at 45°**; the Debug figure leans on one 49.80 ms round.
- **Round-to-round spread on AC is 0.3–2.9 ms**, and 3.4 ms on that case; the
  45° cases spread more than the 0° ones. A later PR's delta has to clear that.
- Zero overflow-drop warnings in the 12 Debug runs. The 12 Release runs cannot
  log one; their poses are vouched for by culling to the Debug counts (786,156
  visible at 0°; 637,025 visible and 1,911,075 axis entries at 45°).
- p99 is not in the table: over 300 frames it is a startup statistic here
  (39–126 ms across runs of one case).

Where the stage-profiling-on Release arm puts its GPU time (sampled invocation
means, not additive into the frame):

| Pose | Largest GPU stages, ms |
|---|---|
| 0° | `voxelStage1` 7.43, `computeLightVolume` 5.35, `voxelStage2` 5.22, `voxelSunFaces` 2.04 |
| 45° | `computeLightVolume` 22.47, `computeVoxelAoPerAxis` 20.39, `lightingOverflow` 10.83, `voxelPerAxisOverflow` 8.93, `voxelPerAxisFinalize` 7.45, `perAxisScatter` 4.71, `voxelPerAxisStore` 3.81 |

The largest CPU system at both poses is `SingleVoxelToCanvasFirst` (4.55 and
5.02 ms per call), then `BuildLightOcclusionGrid` (1.45) and
`UpdateVoxelSetChildren` (1.30).

## What the pieces are

- `configs/perf/million.lua` and `million-profiling-off.lua` carry the whole
  scene, including `config.voxel_pool_edge = 128`: the engine's pre-init pass
  reads the preset after `config.lua`, so no ignored runtime config has to be
  edited by hand and restored. `--wave-freeze` and `--yaw` stay on the command
  line (the first is a CLI-only switch, the second is the axis under test).
- `million_controls.py` runs every case once per round, forward on odd rounds
  and reverse on even. Its summary prints each case's per-round means beside
  the mean, so drift is shown, not averaged away, and it stops if a tree's
  binary, the shaders, the runtime scripts or the power source change under it.
- `repeat_profile.py` records the build tree, its `CMAKE_BUILD_TYPE` and
  `host_power`, counts overflow-drop warnings per run (a test pins the counted
  text to the warning `VOXEL_TO_TRIXEL_STAGE_1` logs), and refuses a run whose
  logged camera pose is not its `--yaw`. Its summary carries p95, p99 and fixed
  updates per rendered frame.

## What each arm means on this host

- **Debug is optimised first-party code over unoptimised dependencies.**
  `engine/CMakeLists.txt` has `target_compile_options(IrredenEngine PUBLIC -O3)`
  with no configuration guard, so in the `macos-debug` tree the 284 engine and
  creation translation units compile `-g -O3`; the other 284 (every third-party
  dependency, the job system, logger and profiler among them, plus the
  standalone tools) get no `-O` flag. In `macos-release` all 568 get
  `-O3 -DNDEBUG`. Release also defines `IR_RELEASE`, which removes asserts,
  every log macro and the `IR_PROFILE_*` scopes. Per-system CPU means for two
  first-party systems matched across the trees to within 0.05 ms at one pose
  (`SingleVoxelToCanvasFirst` 4.38 / 4.33, `UpdateVoxelSetChildren` 1.26 /
  1.26); that says those systems are not dependency-bound, not that the
  trees are equivalent.
- **The Debug stage-profiling-on arm writes a 0.9 GB `profiler_dump.prof` at
  exit** (the Release arm's is 4 KB, its scopes being compiled out). The write
  is after the measured window, but the next case in the matrix starts right
  behind it, an order-correlated load the interleave spreads and does not
  remove.
- **A Release build logs nothing.** `IR_RELEASE` turns every log macro into an
  empty statement, warnings included, so a Release run reports neither its
  camera pose nor an overflow-drop warning. `repeat_profile.py` records both
  as unverified (`null`, never 0) when the log carries no engine logger.
  The matrix vouches for the Release pose another way: with the wave frozen
  the culled counts fingerprint the pose and are in the report, which Release
  still writes, so every build's stage-profiling-on case at one pose must cull
  to the same counts. Overflow drops in Release have no witness yet; the
  Debug arm at the same pose, same shaders and same population is the
  evidence, and putting the drop count in the profile report is the fix.
- **Stage profiling off is not instrumentation off.** The off preset disables
  the CPU scope profiler and the GPU stage timers. `--auto-profile` still turns
  on per-system wall timers and the full-frame GPU timestamps, which is what
  lets an off arm report a GPU frame at all.
- **Presents are display-paced.** The `CAMetalLayer` keeps its default display
  sync. The built-in panel is 120 Hz, so the floor is 8.33 ms, below the
  16.67 ms criterion; frame minima in these runs are not vblank multiples.
- **p99 here is a startup statistic.** The report's percentiles cover all 300
  frames, so p99 is the third-slowest frame and first-frame hitches own it
  (maxima of 90–294 ms against medians of 33–63 ms). The objective's p99 row
  needs warm-up-excluded percentiles or a much longer window; that is D1's
  tail-latency slice, and no p99 below should be read against 25 ms.

## What the first attempts showed

Apple M4 Max, Metal, macOS 26.5.2, **on battery throughout**. Reports and
manifests: [million-controls/](million-controls/).

### Grouped arms drift by more than the differences they test

Eight arms, three 300-frame runs each, run one arm after another
(`grouped-first-attempt/`). These ran before the `--yaw` unit fix
([perf-grid-yaw-unit.md](perf-grid-yaw-unit.md)), so the rotated arm is the
**0.785°** pose. Zero overflow-drop warnings in the 12 Debug runs; the 12
Release runs could not have logged one (their manifests predate the
`engine_logged` field and record a 0 that means nothing).

| Build | Stage profiling | Pose | Frame mean ms (run range) | GPU frame ms | Updates / frame |
|---|---|---:|---:|---:|---:|
| Debug | on | 0° | 33.70 (33.46–33.93) | 21.79 | 2.0 |
| Debug | on | 0.785° | 53.02 (52.34–53.64) | 37.37 | 3.2 |
| Debug | off | 0° | 34.09 (33.57–34.57) | 21.97 | 2.0 |
| Debug | off | 0.785° | 60.96 (59.36–62.86) | 44.10 | 3.6 |
| Release | on | 0° | 42.37 (38.21–45.00) | 29.37 | 2.5 |
| Release | on | 0.785° | 60.98 (59.86–63.05) | 44.50 | 3.7 |
| Release | off | 0° | 37.53 (36.32–39.67) | 25.31 | 2.3 |
| Release | off | 0.785° | 57.02 (56.07–58.37) | 41.12 | 3.4 |

Read top to bottom this says turning stage profiling off costs 8 ms and a
Release build is slower than Debug. Neither is true. The rows are in run order,
and the GPU slowed as the session went on: with byte-identical shaders
(`6c0fed4e4252c227` in every manifest) `computeLightVolume` read 5.62 ms in the
first arm and 7.71 ms in the fifth at the same pose, and `voxelStage1` 7.37 then
10.16 ms. No arm difference in this table is separable from that drift, which is
why the recipe interleaves.

### On battery the same pose moved from 33.7 to 52.9 ms in half an hour

The interleaved matrix was started at 65% charge (91% when the grouped run
ended, twenty-two minutes earlier) and stopped after two runs
(`interleaved-on-battery/`, Debug, stage profiling on, the fixed binary
`1c2cca78ce9a3421`, the fleet benchmark lock held, nothing else running):

| Pose | Frame mean ms | GPU frame ms | `computeLightVolume` ms | Visible | AxisEntries | Updates / frame |
|---|---:|---:|---:|---:|---:|---:|
| 0° | 52.91 | 35.52 | 14.10 | 786,156 | 0 | 3.2 |
| 45° | 58.37 | 40.63 | 29.23 | 637,025 | 1,911,075 | 3.5 |

The 0° scene is the grouped table's first row: same population, same 786,156
visible candidates, same shaders, and `computeLightVolume` takes 14.10 ms where
it took 5.62 ms. The two runs are not a controlled pair. The binaries differ
(`836575ec9df99f19` at `021d41f7f` for the grouped run, `1c2cca78ce9a3421` with
the `--yaw` fix and this slice's init change for the interleaved one; neither
change is on the 0° render path), the manifests carry no timestamps, and
charge level, thermal state and the fleet's preceding build burst all moved
together. Host state is the working hypothesis, not a finding. The AC
reference above reads 34.7 ms for the same Debug arm at 0°, which fits it and
does not prove it (the base moved 33 commits in between). It is why
`host_power` is recorded per manifest and why the reference prints its
per-round means.

One lead survives the conditions, because it compares two runs minutes apart
and rests on counts, not time. At a true 45° the million scene holds 637,025
visible candidates and 1.91M axis entries; the 0.785° pose the corpus measured
holds 901,709 and 2.70M. The rotated-to-cardinal frame ratio inside this round
is 1.10×, where the mislabelled pose gave 1.57× in the grouped table. One pair
taken under those conditions is a reason to measure the objective's rotation-parity row
(today quoted as 2.06× at 64³, also at the 0.785° pose) again, not a number to
replace it with.

## Next measurements

1. This matrix again after any host or OS change, and on an OpenGL host.
2. The overflow-drop count and the camera pose in the profile report, so a
   Release run can witness both.
3. Warm-up-excluded frame percentiles in the profile report, so the p99 row is
   readable.
4. Continuous yaw as a profiled fixture: the objective's criterion is a sweep,
   and the per-pose costs in `perf-grid-yaw-unit.md` (45° 19.7 ms, 58.3°
   14.9 ms, 116.6° 13.1 ms at 64³) show one pose does not stand for the turn.
