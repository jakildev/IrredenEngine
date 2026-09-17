# GPU work attribution under rotation

Two small shader optimizations were tested and rejected: skip sun-map sampling
when the direct Lambert term is zero, and reject same-surface AO neighbors before
world-position reconstruction. Neither establishes a full-frame performance gain.
Production shader sources and staged runtime shaders are restored. Candidate
patches and reports are retained in [gpu-cost-attribution/](gpu-cost-attribution/).

## Method

Apple M4 Max, Metal Debug, frozen IRPerfGrid voxel-set wave, yaw45°, zoom4,
`--no-overlay --auto-profile 300`, stage profiling enabled. Two runs per case,
including startup. Default uses 64³ voxels/pool64; million uses 100³/pool128.
Measurements ran serially with the fleet resource coordinator. Cases are grouped,
not randomized/interleaved; small differences are not wins. GPU figures below
are completed-frame envelopes, equal to summed buffer spans in these single-
submission runs. They include stalls, not just GPU busy execution.

Runtime-only diagnostic patches isolate selected shader bodies:

- AO: use its existing disabled path, writing AO1 and returning before sampling.
- Light propagation: zero neighbor-loop iterations, retaining read/write ping-pong.
- Overflow lighting: return before relighting, retaining appended albedo records.
- Overflow sorting: skip local and strided networks, retaining argument generation
  and sentinel fill. Live records remain valid but lose canonical order.

These change rendering and are not candidate optimizations. Dispatches, resource
setup and other passes remain. Differences are marginal workload costs under
this fixture, not isolated hardware durations or additive budgets. In particular,
a stage row near7.5ms does not establish7.5ms of reclaimable AO work. The probes
show why optimization decisions need full-frame controls.

## Results

| Default case | Frame mean ms (run range) | GPU frame mean ms (run range) |
|---|---:|---:|
| Control | 17.430 (17.420–17.440) | 13.292 (13.290–13.294) |
| Rejected backface skip | 17.580 (17.390–17.770) | 13.256 (13.235–13.277) |
| Rejected AO early rejection | 17.480 (17.470–17.490) | 13.343 (13.321–13.364) |
| AO body disabled | 17.400 (17.370–17.430) | 13.302 (13.277–13.327) |
| Propagation copy only | 15.995 (15.950–16.040) | 11.989 (11.961–12.018) |
| Overflow albedo only | 17.380 (17.370–17.390) | 13.267 (13.252–13.282) |
| Sort network disabled | 16.165 (16.130–16.200) | 12.139 (12.100–12.178) |

| Million case | Frame mean ms (run range) | GPU frame mean ms (run range) |
|---|---:|---:|
| million-control | 50.850 (50.550–51.150) | 35.709 (35.624–35.794) |
| million-sort-disabled | 46.480 (46.120–46.840) | 31.639 (31.516–31.763) |

At one million scene voxels, disabling sorting reduces GPU frame duration by
4.070 ms and overall frame time by 4.370 ms in these grouped controls. This is a
useful optimization target, not a shippable gain: canonical ordering is required.
The remaining 31.639 ms GPU duration also shows sorting alone cannot achieve 60 fps
in this fixture.

Each accepted run has 300/300 valid GPU frames. All runtime shader/configuration
overrides are restored.

## Next experiments

Prioritize sorting work at larger populations and light propagation over the two
rejected arithmetic changes. For sorting, compare algorithms that preserve the
exact `(cell, distance, color)` order and duplicate/tie behavior; do not skip it in
production. For propagation, investigate bounded active tiles while accounting
for off-screen lights, blockers, clearing both ping-pong textures and unchanged
iteration/quantization behavior. Compare full GPU-frame cost before expanding
these changes to the broader rotation/subdivision matrix.

CPU update cadence remains an independent follow-up. Existing multithreading is
not evidence that repeated per-frame work is necessary, but this audit introduces
no threading or simulation changes.

## Evidence and tooling

`repeat_profile.py` now records a startup digest of the runtime scripts directory,
including configuration and presets, alongside binary and shader digests. The
shared digest includes relative filenames and content and is insensitive to file
creation order. This is an identity snapshot, not monitoring during execution or
a record of resolved settings. Tests cover configuration-content changes and
renames; configuration snapshots are retained with these measurements.

The two shader candidates and all four diagnostic body changes are restored;
no visual behavior change ships in this slice. OpenGL runtime measurements remain
outstanding. Native build and clean profiling exits validate the harness; Python
and lint checks validate the tool change.

Restored-renderer check: nine full-turn CanvasStress capture pairs are RGB
identical (1241–1249 versus1250–1258), with a clean native exit. The normal
renderer retains every feature disabled in the diagnostic probes.

| Before experiments | Restored production renderer |
|---|---|
| ![Before](../pr-screenshots/codex/gpu-cost-attribution/capture-1242.png) | ![After](../pr-screenshots/codex/gpu-cost-attribution/capture-1251.png) |
