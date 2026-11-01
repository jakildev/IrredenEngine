# scripts/perf — perf matrix, comparison, and the perf gate

## Explicit capacity envelope

`capacity_envelope.py` executes the checked-in
`capacity_envelope.json` specification into one platform-specific output root.
It composes the million, rotation, surface-lighting, and private-canvas controls
without merging Metal and OpenGL results. Hardware, backend, power/thermal state,
Release build, resolution, populations, visibility, canvas area, subdivision,
shadows, and overflow budgets stay explicit. Use `--dry-run` to inspect the exact
commands before reserving a quiet host. Timed results do not replace the separate
prepass-rejection, receiver-shadow, and crowded-overflow oracles.

The [current qualification](../../docs/perf/capacity-envelope/README.md) records
the measured Metal shortfall and the still-unqualified Windows/OpenGL row.

`perf_grid_matrix.sh` drives `IRPerfGrid` across a matrix of cells and writes
`save_files/perf/<sha>[-<label>]/` (one `<cell>.txt` profile report per cell
plus `manifest.json`). `ir-perf-grid` (`engine/tools/bin/`) is the entry point:
it holds `ir-acquire benchmark` around the whole matrix, captures an
`ir_ref_bench` reading, and splices a `calibration` block — `ref_ms` and the
`ir-host-probe` fingerprint (`host_slug`) — into the manifest.
`compare_perf_runs.py` diffs two runs; `check_regression.py` is the gate.

## Surface-lighting scene controls

```bash
ir-acquire benchmark -- python3 scripts/perf/surface_lighting_controls.py --output /tmp/surface-lighting-controls
```

The fog-free `IRCanvasStress` matrix compares cardinal and continuous-yaw GRID
receivers at zoom 1/4 with 0/8/16 coincident distant source casters. Shadows-off
and unlit pairs keep the geometry and camera fixed. By default every case enables
the `IR_PERAXIS_SURFACE_LIGHTING=1` experiment, recorded in its manifest.
The eight-caster pair also runs at subdivision 4 at both zooms.
Completed `cardinalSurfaceFb`
or `perAxisSurfaceFb` GPU samples are required in every lit report; unlit controls
must have neither. This certifies the selected draw path, not pixel coverage or
shadow correctness. The independent receiver-polygon and crowded-overlap
diagnostics own those checks.
The usual `IRPerfGrid` fog pipeline disables surface lighting, so its perf gate
does not qualify these paths. `--fog-reveal <radius>` instead fogs the main canvas
outside an analytic disc about the origin: every arm must then complete
`fogToTrixel` samples, and a surface-off cardinal arm must keep the compute
`trixelToFb` route. `--no-stage-timing` is the instrumentation control: it records
wall-frame timing with GPU stage timestamps off and skips the route checks, so
pair it with a timed matrix of the same arms.

Use `--adoption-matrix` to compare all four explicit surface-lighting 0/1 ×
visibility-prepass 0/1 arms. Repeated `--case` arguments select a bounded subset,
for example `--case per-axis-zoom1-overlap0 --case per-axis-zoom1-overlap16`.
Every arm overrides ambient values of both switches and disables visibility
statistics to keep diagnostic readbacks outside the timed window. The manifest
must match the requested arm; all other rendering environment, power-source and
runtime artifact identities must agree across the matrix. Missing rounds fail.
Surface-off intermediate-yaw arms require completed compute lighting and scatter
samples; surface-on arms require continuous surface samples without compute
relighting. Cardinal receiving remains active for either surface-switch value.
The combined surface timer does not independently prove prepass execution:
capture its `VISIBILITY-PROBE` counters in a separate untimed stats-enabled run,
alongside inactive surface-off and prepass-off controls.

`--scene grid-overlap` instead selects frozen `maingrid,gridspin,floor` GRID
receivers under explicit oblique sunlight. Its eight cases span yaw 0/0.6,
zoom 1/4 and base subdivisions 1/4; `--adoption-matrix` expands these to 32 arms.
For a bounded comparison, select `--case per-axis-zoom4-receivers-subdiv1`.
Case names belong to their scene; mismatched selections fail before launching.
The default `shadowbox` scene retains its original 20 cases.
The [untimed GRID controls](../../docs/pr-screenshots/codex/grid-receiver-prepass/README.md)
show real fragment rejection with identical prepass-off/on images. Identical
images and counters across the two subdivision settings limit that fixture's
ability to characterize subdivision scaling; they do not qualify throughput.

Results retain runtime fingerprints, pose/overflow witnesses and
forward/reverse rounds through `repeat_profile.py` and `profile_matrix.py`.
Each run uses `--profile-frames 360` (override with the driver's `--frames`)
and exits without screenshots. The runner rejects incomplete frame series,
incorrect pose sample counts and capture flags combined with this mode.
Visual evidence must be captured separately, outside the timed window.
Offscreen frame pacing remains enabled; wall-frame duration includes that wait.
The outer benchmark wrapper's successful quiet verdict is required even when
all individual reports validate. A quiet breach invalidates throughput claims.
The matrix reports full-frame tails and sampled GPU invocation means separately.
Its overflow column is the per-axis raster overflow witness, not sun-face tile
completeness. Cull counts are averages over canvas samples, not unique scene
entity totals; adding private canvases can lower that average.
It does not certify million-entity throughput or treat a fast incomplete shadow
query as correct. Use a new output directory and a configured, freshly built
`IRCanvasStress`.

## Native finite-shadow query probe

On macOS:

```bash
IR_QUIET_REPORT_FILE=/tmp/sun-query-probe.quiet ir-acquire benchmark -- python3 scripts/perf/sun_query_probe.py --output /tmp/sun-query-probe
```

This measures the production cascade sampler against
complete 64-entry inline and 256-entry spill lists. Five controlled cases
separate envelope misses, footprint misses, behind-receiver faces, and first/
last blockers. Every receiver's shadow result is checked; a separate diagnostic
dispatch checks every visit/miss/blocker counter. Timed shaders disable counters.

The reported time is the GPU span of an eight-dispatch command buffer divided
by eight, including dispatch ordering. Exhausted queries also execute empty-map
fallback. These are specialized, coherent single-tile samples: fixed normal,
basis, scale, throw and receiver kind permit compiler simplification. They do
not measure index construction, rasterization, cache diversity or frame rate.
Use them to select hypotheses, then qualify changes in native demo workloads.
Warmup runs for at least three seconds and two full fixture rounds; all rounds
alternate forward/reverse case order. `--warmup-seconds` adjusts the duration.
Inspect sample order for drift even when the quiet-window check passes. Output
retains every sample, expanded shader/launcher source and hashes; the directory
must be empty. Portable fixture and corruption controls live in
`scripts/tests/test_sun_query_probe.py`. Preserve the wrapper exit status and
quiet report alongside results: the wrapper checks coverage after the probe
exits. Only a zero wrapper exit with `GUARDED` qualifies quiet-host evidence;
a results file alone does not.

For native correctness without performance measurement, use `ir-acquire gpu --
python3 scripts/perf/sun_query_probe.py --validate-only --output
/tmp/sun-query-validation`. This checks both shader variants and all ten cells,
but records no timing samples and makes no quiet-host claim.

## Finite-shadow hierarchy probe

`sun_hierarchy_probe.py` validates GPU construction and traversal over captured
finite-quad pools. `--spatial` adds GPU Morton ordering through the shared
per-axis sorter; `--measure` times construction and reference-free queries
separately and requires `ir-acquire benchmark`. The
[experiment and replay instructions](../../docs/perf/sun-spatial-hierarchy/README.md)
describe the controls, batching and limits. This diagnostic does not change
production shadow routing or qualify frame throughput.

`--query-side 257 --repeat-queries 1` submits 66,049 distinct positions rather
than repeating the default 17×17 grid. Both sample the closed sun-UV square
[-12, 12] at depth 2; this remains a planar synthetic workload, not captured
screen receivers. The report records the side length, distinct count and repeat
count. Side lengths are 2..513, repetitions are 1..1024, and the combined query
count is capped at 1,048,576. Compare ordered/unsorted variants with identical
grids and retain quiet-window qualification; a denser grid alone proves no gain.

## Verdicts are per fingerprint

`check_regression.py <baseline_root> <head_dir>` resolves
`<baseline_root>/<head host_slug>/`. A same-slug tip baseline gates (raw
deltas, or load-normalized when the head's `ref_ms` is at least
`LOAD_FACTOR_TRUST_NORMALIZED` times the baseline's). History mode normally
grades a class-matched capture raw. On a host listed in
`CLASS_MATCH_SCALE_FROM`, a head at or above the listed calibration ratio
instead grades only frame `avg` on `head mean × capture ref_ms / head ref_ms`;
p99, GPU-stage, and CPU-system marks stay raw. A different slug or no baseline
is informational and exits 0. A perf verdict therefore says nothing about a
host whose slug has no baseline.

The `perf:improved` verdict is narrower than regression grading. Only a
graded frame `avg` cell contributes, and it must clear the improvement
threshold both raw and after multiplying the head by the capture-to-head
calibration ratio. That ratio can only withhold the label and applies on every
host class; `p99`, GPU-stage, and CPU-system marks remain informational.

On the PR path the improvement must also be attributable to the PR. The run
measures the merge ref, so the head carries every master commit between the
capture's master `k` and the merge's first parent `m`.
`check_regression.py --attribution-base m --attribution-trunk origin/<default>`
writes no improved cells for the first of these that holds:

1. **attribution unavailable**: HEAD is not a two-parent merge, or `m`, `k` or
   the trunk does not resolve to one commit;
2. **stacked**: `m` is not on the trunk, so the run measured a base branch's
   unmerged changes;
3. **capture ahead**: `k` is not an ancestor of `m`;
4. **master improved**: some capture in the history (any slug) and its
   class-matched predecessor form a step whose master range meets `k..m` and
   whose frame `avg` improves on every common cell, raw and calibrated.

The host note's `- attribution:` line names the reason and, for a master step,
its slug, both masters and the per-cell deltas. A master improvement that no
capture had measured when the run read the history is still credited to the
PR.

`replay_attribution.py` re-grades a rule change over the gate's own PR
comments. It fetches the class-matched comments in a `--since`/`--until`
window, resolves each merge ref's first parent through the API, and runs
`resolve_attribution` against the `perf-baseline` history as each run could
see it (captures committed at or before the comment). It prints the reason
totals for every row and for the rows whose frame `avg` would have earned the
label; `--stale-master` / `--stale-after` tally a known set of stale captures.
Run it from a checkout with `origin/master` and `origin/perf-baseline` fetched.

| Host class | `CLASS_MATCH_SCALE_FROM` | Evidence and limit |
|---|---:|---|
| `linux-x86_64-xeon-platinum-8573c-unknown` | 1.10 | Seven PR heads at or above the threshold, on six changes against two captures: scaling puts all 14 frame-average cells inside the 10% gate. The median residual is -1.5% at zoom 1 and -3.4% at zoom 4, so a zoom-4 regression on a runner near 1.17× needs about 13.5% to fail. |

A host class with no entry is graded raw. A class gains an entry only from its
own heads at or above the proposed threshold when every scaled frame-average
delta lands inside `--regress-pct`; evidence from another SKU is not enough.
The table deliberately has no `epyc-7763-64` entry: its 159-head corpus reaches
only a 1.082 ratio and shows no frame-time signal over that measured range.

Baselines live on the `perf-baseline` branch under
`docs/perf/baseline_latest/<slug>/` (`manifest.json`, `host.json`, one
`<cell>.txt` per cell). List them with
`git ls-tree origin/perf-baseline docs/perf/baseline_latest/`. There are two
sources:

- **Hosted Linux runners** (`linux-x86_64-*`): `perf-gate.yml` writes these on
  every master push that touches the gated paths, using the 4-cell
  `--quick --target both` matrix at 60 frames (`IRPerfGrid` first, then
  `IRLuaPerfGrid`, each at zoom 1 and 4).
- **The Windows ship host** (`windows-x86_64-ryzen-9-5950x-rtx-3060-ti`: Ryzen
  9 5950X, RTX 3060 Ti, MSYS2 mingw64, `windows-debug`): seeded by hand with
  the procedure below, using the default 12-cell matrix at 300 frames. No CI
  runner has this fingerprint, so only a run on that machine compares against
  it.

## Split a system row with `IR_PROFILE_SCOPE`

The `--- CPU phase timing ---` section of every cell's report lists the fixed
phases, then one row per `IR_PROFILE_SCOPE(name)` recorded on the main thread
during the profiled window (avg per invocation, max, sample count), sorted by
name. `--auto-profile` turns the histogram on itself (`World::enableFrameTiming`),
so the rows appear with `--no-overlay` and in any creation. To split a system
row on the gate, add a scope inside its `beginTick` / `tick` / `endTick` and read
the new row in the perf-gate artifact. Rows are informational: neither
`compare_perf_runs.py` nor `check_regression.py` gates on them. Scopes recorded
from worker threads are unsupported, and `IR_RELEASE` compiles the macro out.
Executor: `fleet-run IRPerfGrid --auto-profile 120 --no-overlay` with a
temporary scope, then `grep -A30 'CPU phase timing' save_files/profile_report.txt`.

## Repeat-profile an `IRShapeDebug` comparison

`repeat_profile.py --target IRShapeDebug --output <dir> --repeats N -- <args>`
runs the fixed-pose SDF comparison N times and writes `summary.md`: each
metric's mean and min–max over the per-run means, and a one-sided 95% upper bound
on that mean (mean + t(0.95, N−1) · sd / √N; `n/a` at one repeat). Only these ShapeDebug
profile arguments are accepted; any other flag fails with `unsupported ShapeDebug
profile argument`:

Use `--release` to select the worktree's `build-release` tree. A repeated
`--render-env NAME=VALUE` sets and records approved render experiment variables
without an environment prefix, which keeps display-backed Codex runs eligible
for their narrow launcher permission.

- required: `--auto-profile <frames>`, `--yaw`, `--zoom`,
  `--sdf-voxel-scene direct|baked`, `--config-preset configs/perf/sdf-on.lua|sdf-off.lua`
- optional workload: `--sdf-voxel-pose rest|root|parented`,
  `--sdf-voxel-presentation grid|detached|revoxelized`,
  `--sdf-voxel-density 1|2`, `--sdf-voxel-copies 1|4|16`,
  `--sdf-voxel-overlap`, `--sdf-voxel-motion`
- optional lighting: `--sdf-voxel-blockers`, `--no-local-lights`, `--no-ao`,
  `--no-shadows`
- optional fixture: `--glitch-test <mode>` (`standard-off`, `off`, `tear`, `hold`,
  `split`, `bars`, `cells`, `depth-tear`, `depth-hold`, `hold-divisor`, `all`).
  The mode is recorded in the manifest's `configuration_sha256` block, so runs of
  different fixtures never compare.

Executor: `python3 scripts/perf/repeat_profile.py --target IRShapeDebug --output <dir>
--repeats 2 -- --glitch-test all --auto-profile 60 --yaw 0 --zoom 4
--sdf-voxel-scene direct --config-preset configs/perf/sdf-on.lua`; the summary then
carries a `GPU framebufferGlitch` row.

`sdf_controls.py` owns the paired matrices. Its default `qualification` suite
varies density, population, overlap, root/parent motion, GRID/rigid/revoxelized
presentation and isolated local light, each with GPU stage timing on and off.
Use `--suite baseline` for the retained 16-case yaw/shadow reproduction, and
repeat `--case <name>` to run a bounded subset. Pass `--release` to select the
worktree's supported Release tree directly.
Every accepted ShapeDebug run
must carry exactly one Release-compatible `SDF-OCCUPANCY` log witness; the
runner retains its bake time, slot/occupied counts, dense-record bytes, exact
bake scratch and process samples in the manifest. It also requires one
`SDF-RESIDENCY` witness after the measured frames. `sdf_controls.py` writes a
`memory-summary.md` that keeps those exact payload figures separate from process
RSS and GPU observations. Metal reports the process's current driver allocation;
OpenGL reports device-wide capacity and availability only when
`GL_NVX_gpu_memory_info` is present, not process-owned residency.

## Same-runner Linux diagnostic

When a historical CI comparison cannot isolate a change, dispatch `Perf Gate`
on the candidate branch with `diagnostic_base` set to the full base commit SHA:

```bash
gh workflow run perf-gate.yml --ref <candidate-branch> -f diagnostic_base=<base-sha>
```

A nonempty `diagnostic_base` disables the baseline writer. The job captures
head/base/head quick matrices on one hosted runner, rebuilding each ref and
refreshing runtime assets before measurement. `perf-run-<run-id>` retains all
raw reports plus `paired/` comparisons and full commit, binary and shader
fingerprints. Both total and steady frame means are reported; comparisons are
informational and do not override the PR gate. Repeated head runs expose drift,
but one base run is not a statistical performance guarantee. Hosted Mesa results
do not establish performance on hardware OpenGL or Metal.

`ci_pair.py` only switches refs in a clean GitHub-hosted checkout and restores
the original head on failure. It is not a local-worktree profiling command.
An empty diagnostic input retains the existing baseline-publishing behavior.

## Windows ship-host baseline

A human or the architect runs this on the ship host when cued. A fleet worker
never refreshes the baseline.

**Refresh when:**

- a merged PR labelled `perf:improved`, after that head's `perf-gate` run has
  concluded, touches `engine/render/` or the GL backend;
- the NVIDIA driver is updated. The fingerprint does not change, but shader
  compile and dispatch behaviour do;
- `engine/math/` changes. `ir-perf-grid` recalibrates `ir_ref_bench` then,
  and the old baseline's `ref_ms` no longer matches the new calibration.

**Exclusivity.** `ir-perf-grid` holds `ir-acquire benchmark` (CPU budget−1,
gpu, perf) for the whole matrix, and each cell's `fleet-run` runs inside that
hold, so queued fleet builds and demo runs wait behind it. That is not
enough on the ship host, which is also an interactive desktop. With the
satellite fleet live and desktop apps open, four back-to-back runs of one
master SHA spread 10–43% per cell (median 25%), which is wider than the
10% gate. One run read load factor 1.01 at its start and still ran every
cell 10–40% slow: `ref_ms` is sampled once, before the first cell, and does
not see load that arrives mid-matrix (the per-cell load gate below does, for
CPU load). So: `fleet-down`, close GPU- and
CPU-heavy desktop apps, and publish only a run that passes the self-check
below.

**`load_ref_ms`.** Each cell's manifest entry carries `ir_ref_bench`
readings taken inside the hold: `before`, the median of three back-to-back
samples just ahead of the cell's run (the samples, in order, are
`before_samples`), and `after`, one sample as soon as the run returns.

- `after` is not a load reading. It is sampled the moment the run returns,
  when the cell's own process can still be tearing down, so a high value
  there says nothing about the host. No consumer may grade on it.
- `before` is the load gate's only input.

**Load gate.** `check_regression.py` classifies every cell of both the head
and the baseline manifest from its `before` reading, on measured host
classes only:

| Host class | Slug glob(s) in `LOAD_GATE_THRESHOLDS` | Threshold `T` | Quiet corpus |
|---|---|---|---|
| GitHub-hosted runners (no GPU) | `linux-x86_64-epyc-*-unknown`, `linux-x86_64-xeon-*-unknown` | 1.10 | every bracketed capture on `perf-baseline` through 2026-10-11: 26 captures, 52 cells, `Q_max` 1.0488 |

The Windows ship host (`windows-x86_64-ryzen-9-5950x-rtx-3060-ti`) gets its
entry once three quiet default-matrix runs, taken with the fleet down, give
it a corpus.

- A cell's factor is its `before` divided by the run floor, the smallest of
  the manifest's `calibration.ref_ms` and every cell's `before`. The start
  reading is taken outside the hold, so load already present there cannot
  hide itself by raising the floor.
- A cell is load-flagged when its factor is at or above `T`. A flagged cell,
  on either side, is informational: it is left out of regression grading, the
  stderr verdict lists it, the comparison gains a "load-flagged cells"
  section with its reading, floor, factor and threshold, and its rows carry no
  ⚠ or ↓. Exit 1 still means an unflagged cell regressed.
- A slug that no glob matches is never classified and grades exactly as it
  would without brackets. That covers every macOS host, where a single
  `before` sample read up to 1.14× on a quiet host and only 1.16× under full
  CPU saturation, and a Linux fleet host whose GPU probe reads `unknown`.
  No `darwin-*` or `macos-*` entry is ever added.
- A cell with a missing, non-positive or malformed `before` is graded as
  without brackets; legacy and unbracketed manifests are untouched.
- `T = max(1.05, 1 + 2 × (Q_max − 1))`, rounded up to 0.01, where `Q_max` is
  the largest `before` factor in the class's quiet corpus. The 1.05 floor is
  half the 10% regression threshold. A class whose rule yields more than
  `LOAD_FACTOR_TRUST_NORMALIZED` (1.20) gets no entry.
- Not detected: GPU-only contention (on macOS it slowed cells 11% with every
  endpoint at or under 1.009×), load that starts and ends inside one cell,
  and load constant across the whole run, which raises the floor with every
  reading and is the run-level load factor's case. The hosted entry rests on
  a quiet corpus alone: nothing has measured `before` rising under a
  noisy-neighbour load on a hosted runner, so the gate claims no detection
  there. It is safe because a gate that does not fire grades exactly as
  without it, and the quiet corpus bounds false flags.
- `--strict-load` makes any load-flagged cell in either manifest a
  measurement error: exit 2, the flagged cells on stderr, no table and no
  verdict.

**Wall time** on the ship host: about 220 s per matrix run (12 cells at
~18 s each, plus the calibration probe). A seed plus its self-check is
under 8 minutes.

Steps (every path is relative to a clean, detached checkout of the measured
`origin/master` SHA, never a feature branch or dirty tree — the manifest
records `git_sha` and `git_dirty`):

```bash
git worktree add --detach <scratch>/master origin/master
cd <scratch>/master
cmake --preset windows-debug
ir-build --target ir_ref_bench && ir-build --target IRPerfGrid
ir-host-probe --refresh           # the cached fingerprint never expires on its own
ir-perf-grid --label baseline     # → save_files/perf/<sha>-baseline/
ir-perf-grid --label selfcheck    # same SHA, immediately after
mkdir -p <scratch>/seed && cp -r save_files/perf/<sha>-baseline <scratch>/seed/<slug>
python3 scripts/perf/check_regression.py --strict-load <scratch>/seed save_files/perf/<sha>-selfcheck
```

`--strict-load` keeps a loaded self-check from passing by having its cells
excused: an exit 2 there names the cells that ran under load in either run.

Publish only when that self-check exits 0, every cell's `status` in both
manifests is `ok`, and the load factor is below
`LOAD_FACTOR_TRUST_NORMALIZED` (1.20). The load factor is
`calibration.ref_ms` divided by the calibrated median in
`~/.cache/irreden/calibration/<slug>.json`. A failing self-check means the
host was not quiet. Find the load and re-run both matrices. Never pick a
passing pair out of several runs.

Commit the run to `perf-baseline` in the CI writer's layout. That is the
"Update baseline on the perf-baseline branch" step of `perf-gate.yml`:
copy the run minus `*.log` and `.cell_marker`, and write the manifest's
`calibration.host_fingerprint` as `host.json`:

```bash
RUN=save_files/perf/<sha>-baseline
SLUG=windows-x86_64-ryzen-9-5950x-rtx-3060-ti
git fetch origin perf-baseline
git worktree add --detach <scratch>/pb origin/perf-baseline
mkdir -p <scratch>/pb/docs/perf/baseline_latest/$SLUG
cp "$RUN"/manifest.json "$RUN"/*.txt <scratch>/pb/docs/perf/baseline_latest/$SLUG/
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); json.dump(m["calibration"]["host_fingerprint"], open(sys.argv[2],"w"), indent=2, sort_keys=True)' \
    "$RUN/manifest.json" <scratch>/pb/docs/perf/baseline_latest/$SLUG/host.json
git -C <scratch>/pb add -A docs/perf/baseline_latest/$SLUG
git -C <scratch>/pb commit -m "perf: seed Windows ship-host baseline ($SLUG) <date>@<sha>" \
    -m "Driver <NVIDIA version>; ref_ms <ref_ms>; load factor <lf>."
git -C <scratch>/pb push origin HEAD:refs/heads/perf-baseline   # never --force
```

The branch is the audit trail and is append-only. If the push is refused,
fetch, re-create the worktree on the new tip, and repeat the copy.

## Compare a PR head on the ship host

```bash
git worktree add --detach <scratch>/pr <pr-head-sha>      # build it as above
cd <scratch>/pr && ir-perf-grid --label pr<N>
mkdir -p <scratch>/base
git archive origin/perf-baseline docs/perf/baseline_latest | tar -x -C <scratch>/base
python3 scripts/perf/check_regression.py \
    <scratch>/base/docs/perf/baseline_latest save_files/perf/<sha>-pr<N>
```

Exit 0 with a per-cell table means no cell regressed past `--regress-pct`
(default 10). Exit 1 names the regressed cells. The informational
"seeding new baseline" or "host mismatch" banner means the head's slug did
not match, so nothing was compared. Run the head the same way as the
baseline: same preset, same default matrix, same quiet fleet.
