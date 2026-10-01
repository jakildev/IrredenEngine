# Rotation and density frame-budget controls

These measurements do **not** meet the general 60 Hz target. The solid-block
control is mostly within budget, but dense rotated geometry exceeds it, and
high cardinal subdivisions produce much larger stalls. No renderer settings
or production shaders were changed for this measurement.

## Reproduction and retained evidence

```bash
python3 scripts/perf/rotation_controls.py --suite motion --rounds 3 --frames 180 --output /tmp/motion-density
```

54 clean native Metal runs: 18 cases, three rounds, forward/reverse/forward
ordering, 180 frames per run. Each report excludes its first 45 frames from
steady statistics; the percentile table pools the remaining 405 samples per
case. Sweeps sample 0 through 358 degrees in two-degree steps, with an explicit
origin pivot and fixed geometry. All runs include shadows and stage profiling;
the optional occlusion-cull experiment remains off. This is a Debug build on a
14-CPU macOS host on AC power, not a Release throughput qualification.

The first attempt completed seven runs, then timed out waiting for CPU slots
before launching the eighth. The remaining cases resumed in the original order
under one `ir-acquire benchmark` reservation. The failed queue attempt is retained
separately and contributes no timing. The fleet remained active: recorded
one-minute host loads range from 4.12 to 46.69, including preceding build work.
Resource reservations reduce contention but do not establish a quiet-host result.

All accepted manifests share head `08f94d66c59fca6fe5bc811f4f0037cd259b2d05`,
binary `b1db6a575dcf5439acc889deb39025c2840a079c591594cb50fea68e5c82c6aa`,
shaders `fa9b60b7fe6c73d3df68cb30361d591d9631f6c45540e9efb483d9f056541344`,
and runtime scripts `85a4c4c0e04ff77ce830640f6b8935b4ab8dcad11f1ff0dfccbc3dd918d927f6`.
Dirty changes during profiling are tooling/docs only. All six recorded render
experiment switches plus overflow logging are unset. Provenance verification
rejects mixtures of those settings, fingerprints or power sources.

- [Cases and arguments](cases.json)
- [Means, GPU envelopes and retained work](summary.md)
- [Frame-budget tails and overflow counts](budget-summary.md)
- [Sampled GPU scopes](gpu-summary.md)
- [Raw reports, logs and manifests](raw.tar.gz), SHA-256
  `70d233271d1a68468b4a7286257b001a7e0bea81c15e7c5fe4e6b74834699d77`

Extract the archive into an empty directory to inspect individual
`<case>/round-<n>/run-1.txt` reports and their manifests. The archive also retains
the unused `queue-timeout-before-resume/` attempt.

## Results

Each scene spawns 262,144 voxel entities. The solid block carries authored
cross-set occlusion masks and retains about 23–24 thousand candidates; the frozen
per-cell wave exposes more faces and retains about 259–262 thousand. Comparing
their cost does not isolate rotation alone. Compare poses within each scene.

| Frozen wave, zoom / base subdivisions | Pose | Steady mean ms | Steady p99 ms | Frames above 16.667 ms |
|---|---|---:|---:|---:|
| 1 / 1 | cardinal | 8.360 | 9.945 | 0 / 405 |
| 1 / 1 | diagonal | 18.217 | 19.469 | 405 / 405 |
| 1 / 1 | sweep | 18.760 | 20.890 | 396 / 405 |
| 1 / 4 | cardinal | 10.400 | 13.469 | 1 / 405 |
| 1 / 4 | diagonal | 18.650 | 21.846 | 405 / 405 |
| 1 / 4 | sweep | 18.890 | 21.394 | 396 / 405 |
| 4 / 4 | cardinal | 88.733 | 90.849 | 405 / 405 |
| 4 / 4 | diagonal | 17.673 | 18.939 | 405 / 405 |
| 4 / 4 | sweep | 20.250 | 91.804 | 405 / 405 |

The nine solid-block cases have steady means of 8.377–9.370 ms, with two budget
misses among 3,645 steady samples. Their overflow is zero. Dense rotated cases
reach 749,109 overflow records at zoom 1 and about 745 thousand at zoom 4, with
**zero dropped records in every accepted run**. Their GPU frame envelopes also
rise (zoom 1/base 1: 4.965 ms cardinal, 14.187 ms diagonal), so the rotation gap
is not explained solely by CPU update cost.

Density is not equal across routes. The startup/cap logs record:

| Zoom / base | Global effective subdivisions | Rotating per-axis cap |
|---|---:|---:|
| 1 / 1 | 1 | 1 (no reduction) |
| 1 / 4 | 4 | 2 |
| 4 / 4 | 16 | 8 |

The high-zoom diagonal result therefore does not establish equal-density parity.
The high-zoom sweep's long tail also matters: its first run's largest steady
samples are approximately 90–94 ms around the cardinal crossings. A mean alone
would hide those stalls.

GPU stage rows are **not additive**: the per-axis multi-encoder scopes overlap.
The larger light-volume or AO row cannot by itself assign the rotation delta to
that kernel. Use the [timing contract](../../design/gpu-stage-timing-cost-model.md)
and isolate a pass before attributing an optimization's benefit.

## Next implementation targets

1. Reduce cardinal work at high effective density while preserving displayed
   geometry and shadow feeders. Require fixed-pose and cardinal-crossing captures
   before changing culling or density behavior.
2. Reduce conservative overflow production/processing using finite-footprint
   visibility proofs. Do not reinstate face-origin-only rejection: it previously
   removed visible slivers. Measure sort/scatter/lighting separately with bounded
   diagnostic controls before choosing the optimization.
3. Repeat accepted changes with profiling disabled, a Release build, a quiet host
   and million-entity controls. This smaller Debug matrix is a bottleneck witness,
   not evidence that the million-at-60 goal is met.
