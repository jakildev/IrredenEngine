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
- The profile report carries a **run witness**: the camera yaw of the first
  and last rendered frame and the yaw travelled between them, the zoom, and the
  per-axis overflow lane's worst frame (live entries, dropped entries, cap,
  frames sampled). `VOXEL_TO_TRIXEL_STAGE_1` records it every frame whether or
  not stage profiling is on, and every build type writes it. The report also
  prints a `Steady frame time` line over the frames after the first quarter,
  and the frame series itself.
- `repeat_profile.py` records the build tree, its `CMAKE_BUILD_TYPE`,
  `host_power`, `host_cpus`, and each run's start time, battery charge and
  one-minute load average; the matrix summary opens with the load range,
  because the benchmark lock excludes cooperating builds and nothing else. It
  reads the
  witness, not the log: it refuses a run whose first or last rendered frame is
  not at its `--yaw`, whose camera moved during a static pose, whose overflow
  list dropped an entry in any frame, whose rotated pose never sampled the
  overflow lane, or whose report carries no witness at all. Its summary adds
  steady-state rows and a tail pooled over every run's steady frames.

## Witnessed round

Every arm vouches for itself; the milliseconds are not a timing reference.
Head `a76b630e8` plus the run-witness change, AC power, one interleaved round
of 300 frames, all eight runs `RESULT=CLEAN`. Shaders `6a13afcaf26f7615`, which
is not the AC reference's set: master moved the shaders this fixture runs in
between. **The fleet was live: load at run start was 7.0 to 12.8 on 14 CPUs**
(an earlier attempt the same hour saw 19.4 and the same frozen 0° scene read
36.8 then 45.5 ms with its frame minimum unchanged at 33 to 34 ms; the
benchmark lock excludes cooperating builds and nothing else). Read the
witness columns; do not diff the milliseconds. Reports and manifests:
[million-controls/witnessed-live-fleet/](million-controls/witnessed-live-fleet/).

| Build | Stage profiling | Logs? | Witnessed yaw (travel) | Overflow max entries / dropped / cap (frames sampled) | Steady p99 ms | All-frames p99 ms | Frame mean ms |
|---|---|---|---:|---:|---:|---:|---:|
| Release | off | no | 0.000° (0.000) | lane idle (0) | 40.46 | 52.58 | 36.50 |
| Release | off | no | 45.000° (0.000) | 2,208,000 / **0** / 8,388,608 (300) | 56.81 | 80.85 | 47.66 |
| Release | on | no | 0.000° (0.000) | lane idle (0) | 40.89 | 61.55 | 37.70 |
| Release | on | no | 45.000° (0.000) | 2,208,000 / **0** / 8,388,608 (300) | 51.21 | 87.04 | 48.59 |
| Debug | off | yes | 0.000° (0.000) | lane idle (0) | 39.62 | 42.30 | 36.39 |
| Debug | off | yes | 45.000° (0.000) | 2,208,000 / **0** / 8,388,608 (300) | 51.30 | 65.40 | 48.32 |
| Debug | on | yes | 0.000° (0.000) | lane idle (0) | 49.84 | 68.77 | 42.76 |
| Debug | on | yes | 45.000° (0.000) | 2,208,000 / **0** / 8,388,608 (300) | 51.97 | 69.65 | 49.44 |

What it says:

- **The objective's arm now carries its own proof.** Release with stage
  profiling off logs nothing, and its report states it rendered every frame at
  45.000° and dropped nothing. The AC reference could say neither for its 12
  Release runs.
- **Zero overflow drops at a million is now a measurement in all four rotated
  arms**, not an inference from the Debug log. The lane peaks at **2,208,000
  entries of a cap of 8,388,608, 26%**: 25 MiB live in a 96 MiB buffer at
  twelve bytes an entry. That is D2's first residency number for the
  paged-versus-larger-pool call.
- **The all-frames p99 overstates the steady tail by 3 to 36 ms** across
  these eight runs. One round holds 225 steady frames, two above its p99, so
  these tails demonstrate the line and are not the objective's p99 row.
- The frame means sit 1 to 8 ms above the AC reference. With the load range
  beside them that is the fleet, and no claim about master's shader change can
  be read out of it.

## The run witness and its controls

Each counter was shown to move when the thing it measures moves, on the
binaries the witnessed round was taken with (64³ default scene, zoom 4, frozen
wave).

| Control | Build | What the report read | Verdict |
|---|---|---|---|
| Static poses `--yaw` 0, 0.785398163, 1.0175, −2.5 rad | Release | yaw first = last = 0.000, 45.000, 58.298, −143.239°, travel 0.000 | each pose witnessed by a build that logs nothing |
| Overflow lane at those poses | Release | 0 samples at 0°; 630,842 / 71,115 / 79,794 max entries, 0 dropped, cap 1,048,576, every frame sampled | the cardinal pose never allocates the lane; the rotated ones sample it each frame |
| `overflowCap_` forced to 65,536 (local patch, not committed) at 45° | Debug | max entries 65,536, **max dropped 565,306** = 630,842 − 65,536 | `repeat_profile.py` refused the run and named the count and the cap; the log carried 2 warning lines, which is what the old count would have reported |
| `--yaw-ramp --auto-screenshot 4`, 400 frames | Release | first 0.000°, last −93.000°, **travel 267.000°**, lane sampled on 341 of 400 frames | the arc is summed through the ±180° seam (267° is −93°), and the 59 cardinal-pose frames have no lane |
| 60 frames at 45° | Debug | all-frames p99 102.09 ms (frame one); steady p99 20.97 ms over the last 45 | the all-frames tail is the startup hitch |

`--yaw-ramp` is an auto-screenshot shot table and moves nothing without
`--auto-screenshot`, so the tool treats a `--yaw` run as a static pose unless
both are present.

The unit suites pin the rest: `test/render/run_witness_test.cpp` (travel
through the seam, a return to the starting pose still travels, the worst
overflow frame is kept and not the last, reset), `test/profile/` (the steady
line's exclusion and the written lines), and the `scripts/perf` tests, one of
which asserts the parser's patterns are the literal format strings in
`profile_report.cpp`, so a reworded line cannot read as a clean run.

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
- **A Release build logs nothing, and vouches for itself anyway.**
  `IR_RELEASE` turns every log macro into an empty statement, warnings
  included, so the log is no witness. The report's run witness is, in every
  build and on every arm, stage profiling off included. The cross-build check
  stays for a different reason: with the wave frozen the culled counts
  fingerprint the scene at a pose, so two trees that disagree were not built
  from one source. `engine_logged` stays in the manifest for the same kind of
  reason: a Release tree that logs, or a Debug tree that does not, is not the
  build its cache says.
- **Stage profiling off is not instrumentation off.** The off preset disables
  the CPU scope profiler and the GPU stage timers. `--auto-profile` still turns
  on per-system wall timers and the full-frame GPU timestamps, which is what
  lets an off arm report a GPU frame at all.
- **Presents are display-paced.** The `CAMetalLayer` keeps its default display
  sync. The built-in panel is 120 Hz, so the floor is 8.33 ms, below the
  16.67 ms criterion; frame minima in these runs are not vblank multiples.
- **The all-frames p99 is a startup statistic; read the steady one.** The
  report's first percentile line covers all 300 frames, so its p99 is the
  third-slowest frame and first-frame hitches own it (maxima of 90–294 ms
  against medians of 33–63 ms). The `Steady frame time` line drops the first
  quarter of the frames (`kProfileWarmupDivisor`, the share IRPerfGrid's own
  auto-profile mean already discards), and the matrix pools the steady frames
  of every round, so a case's p99 rests on 675 frames with six above it. The
  AC reference above predates the line and carries no readable p99.

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

1. The three-round matrix again **on a quiet host**: master has moved the
   shaders this fixture runs (`ir_projected_face`, the sun-face query layout)
   since the AC reference, and the witnessed round below was taken under a live
   fleet, so neither is the table a D2 change diffs against.
2. This matrix after any host or OS change, and on an OpenGL host.
3. A longer window for the tail: 225 steady frames put two frames above a p99.
4. Continuous yaw as a profiled fixture: the objective's criterion is a sweep,
   and the per-pose costs in `perf-grid-yaw-unit.md` (45° 19.7 ms, 58.3°
   14.9 ms, 116.6° 13.1 ms at 64³) show one pose does not stand for the turn.
