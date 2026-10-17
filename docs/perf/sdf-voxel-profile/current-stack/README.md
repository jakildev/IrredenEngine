# Guarded SDF and baked-voxel cost baseline

Both 48-run matrices completed on Metal: Debug and Release each cross direct
SDF/baked occupancy, yaw 0/45, sun shadows on/off and stage timing on/off, with
three rounds in forward/reverse/forward case order. Baked geometry has a lower
cell-mean GPU envelope in all sixteen build/pose/shadow/timing combinations.
Some individual pairs reverse, and diagonal wall-frame results are mixed.
This is an observational baseline, not an optimization or a general speedup
verdict. No rendering default or shader changes accompany this record.

## Scope and provenance

- Source: `a371b9f4391b819b20007d1bc05490e727ee8745`, clean during profiles;
  master ancestor `741d3f0ab`. Profiles preceded the include-only cleanup and the later master integration
  (`19e87b5d2`, including the shared-edge scatter fix). These numbers remain
  evidence for the recorded source, not fresh measurements of that integration.
- Apple M4 Max, 14 CPU cores, macOS 26.5.2, Metal, **battery power** throughout.
  Debug and Release are separate comparisons. Artifact/configuration hashes,
  host data and exact rendering environment are in [manifest.json](manifest.json).
- IRShapeDebug: sphere, cylinder, cone and torus, four six-voxel blockers,
  a 48×48×1 floor and one local light. AO off; default lighting; continuous
  receiving and its visibility prepass unset. Rest pose, ORIGIN pivot, zoom 4,
  effective subdivisions 4, 1280×720 game resolution, offscreen window mode.
- Each run records 300 frames. Wall-frame steady statistics discard the first
  75, leaving 21,600 steady frames across 96 accepted runs. GPU envelope/stage
  aggregates cover the report's whole sampling interval, not that trimmed series.
- All accepted runs are fresh, exit 0, clean and `QUIET=guarded`, with stable
  artifacts/configuration, requested pose, GPU measurements and zero overflow
  drops. The quiet guard does not control battery state or thermal drift.

These representations intentionally differ: direct curves are sampled SDF
surfaces; the baked subjects are finite occupancy at their authored density.
They share descriptors, placements and lighting, not identical pixel coverage.
[Twelve context images](../../../pr-screenshots/codex/sdf-voxel-cost-baseline/README.md)
show that distinction and the remaining coarse floor-shadow steps. They are not
before/after images of a fix or an absolute correctness approval.

## Measurements

[Timing-off comparison](timing-off-comparison.md) gives means and paired GPU
delta ranges; [paired-comparison.json](paired-comparison.json) retains every
value, including timing-on arms. In Release with shadows on and stage timing
off, direct/baked GPU spans average **3.904/3.323 ms at yaw 0** and
**5.629/5.161 ms at yaw 45**. The corresponding steady wall means are
**8.697/8.370 ms** and **15.213/16.423 ms**. The latter reversal is why GPU
span alone cannot establish an end-to-end win.

[Debug](debug-summary.md) and [Release](release-summary.md) summaries retain
frame distributions; their GPU and budget summaries retain stage ranges and
geometry counts. [Selected stages](selected-stages.md) aid attribution, but
Metal timestamp intervals can overlap: summing stages is not a frame budget.
Three rounds here are not the five-pair acceptance test for a proposed stage
optimization. Do not infer a zero geometry count from timing-off cull rows;
use the timing-on witnesses.

Both representations report 213 total engine entities, including fixture and
GUI internals. Direct uses 2,328 pooled voxels for floor/blockers; baked uses
7,652 total, with 3,134 retained versus 2,328 direct. Subject baking allocates
5,324 slots, 1,459 occupied, and reports 63,888 record bytes. Those bytes are
not total process or GPU residency. At yaw 45 the axis-entry counts are
2,560 direct and 4,008 baked, with maximum overflow counts 12/403 and zero
drops. This four-subject scene does not qualify dense overlap or large worlds.

## CPU attribution and rejected data

Separate Debug yaw-45, shadows-on, timing-off runs used a three-second
`/usr/bin/sample` after a two-second delay. Main-thread wall samples were
85.5% direct and 86.0% baked in GPU completion waits plus offscreen pacing.
`MetalRenderDevice::present` waits for completion, then `paceOffscreenFrame`
sleeps to the display interval. These are stack residence fractions, not CPU
utilization. They do not justify rewriting simulation updates or removing
pacing. Sampling overhead excludes these two runs from the timing comparison.

The first Release attempt has 46 manifests: 45 clean guarded runs and one
quiet-window breach (`RESULT=CONTAMINATED`, exit 76). The entire attempt is
excluded, including its partial two-round summaries. A fresh full retry supplied
the accepted Release matrix; no failed case was overwritten or cherry-picked.

## Reproduction and retained evidence

Run from the dedicated worktree after building IRShapeDebug in each configured
build directory. Output directories below must be fresh. GUI access and a quiet
host are required; keep the guard enabled.

```bash
python3 scripts/perf/sdf_controls.py --output /tmp/sdf-debug --rounds 3 --frames 300 --zoom 4
env IRREDEN_BUILD_DIR=build-release python3 scripts/perf/sdf_controls.py --output /tmp/sdf-release --rounds 3 --frames 300 --zoom 4
python3 scripts/perf/repeat_profile.py --target IRShapeDebug --output /tmp/sdf-cpu-direct --repeats 1 --cpu-sample-seconds 3 --cpu-sample-delay 2 -- --auto-profile 1200 --zoom 4 --sdf-voxel-blockers --no-ao --window-mode offscreen --sdf-voxel-scene direct --yaw 0.7853981633974483 --config-preset configs/perf/sdf-off.lua
```

Repeat the CPU command with `baked` and a distinct output directory. Each
`*-raw.tar.gz` contains original numeric reports, normalized logs/manifests and
runner summaries. `debug`/`release` are accepted; `cpu-*` are sampled attribution;
`rejected-release` is excluded. Only local paths and ANSI escapes are normalized.
The manifest hashes each archive and every member. Extract into a fresh directory
with `tar -xzf <archive> -C <directory>`; members are relative paths.
The existing `compare_perf_runs.parse_report` reads `run-1.txt`; each table cell
is the arithmetic mean of its three reports, and paired ranges use baked minus
direct within the same round. The archives retain individual percentiles. The extracted Release GPU summary
uses a corrected scope sentence (general throughput); its archived original
retains the runner’s static Release disclaimer. Numeric rows are identical.

## Next measurements

1. Instrument post-prepass candidate visits, misses and first blockers for
   continuous receiving; use these to select a query optimization, then time
   it with counters disabled. This default-lighting matrix does not measure it.
2. Sweep density, overlap, camera/object motion and population independently.
   Measure startup baking and total resident CPU/GPU memory separately.
3. Repeat on AC power, Windows/OpenGL and presented-window controls before
   broader throughput claims. No million-visible-entity/60-FPS result is implied.
