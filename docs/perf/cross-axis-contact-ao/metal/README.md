# Cross-axis contact AO — Metal cost evidence

Five interleaved parent/head pairs profile the Release million-voxel control at
a fixed 45-degree yaw under the benchmark lease. Each arm requested 301 frames;
the retained reports contain at least 300 timed frames because the terminal
auto-profile frame may trigger report generation before its camera witness is
appended. All runs used AC power and unchanged per-arm binary, shader, and
runtime-script identities. Raw reports and manifests are retained under
`pair-N/{parent,head}`; `comparison.json` contains the parsed inputs and rule-3
calculation.

The treatment performs exactly four additional existing-distance-texture reads
per active rotating AO cell. It creates no texture or buffer. The surviving
overflow record had no spare representation, so its packed scratch record grows
from three uints (12 bytes) to four uints (16 bytes): **+4 bytes per surviving
overflow record**. The separate overflow lane measures that bandwidth cost.

## Grade

| Row | Parent | Head | Ratio | Paired delta | Base spread | Verdict |
|---|---:|---:|---:|---:|---:|---|
| frame mean | 11.3600 ms | 11.3700 ms | 1.001x | +0.0100 ms | 0.2100 ms | pass (i) |
| frame p99 | 12.2520 ms | 12.3860 ms | 1.011x | +0.1340 ms | 1.5700 ms | pass (i) |
| GPU frame envelope | 10.2692 ms | 10.2584 ms | 0.999x | -0.0108 ms | 0.1580 ms | pass (i) |
| `computeVoxelAoPerAxis` | 0.0316 ms | 0.0464 ms | 1.468x | +0.0148 ms | 0.0050 ms | pass (i), +0.020 ms absolute treatment budget |
| `lightingOverflow` | 0.1160 ms | 0.1214 ms | 1.047x | +0.0054 ms | 0.0020 ms | pass (i), 1.10x / +0.010 ms budget |

The AO row's ratio is large because the base row is only 31.6 microseconds; its
14.8-microsecond measured increase is below the explicit 20-microsecond budget,
which is under 0.2% of this control's GPU-frame envelope. The overflow lane is
within both its ratio and absolute limits. Frame rows pass rule (i); no fallback
verdict is used.

## Reproduction

The committed driver alternates arm order and applies the grading rule:

```sh
python3 scripts/perf/cross_axis_contact_ao_timing.py \
  --output docs/perf/cross-axis-contact-ao/metal \
  --parent-build build-release-parent --head-build build-release-head \
  --parent-commit e5c0ce713 --head-commit 1ce3d77be \
  --pairs 5 --frames 300
```

The evidence in this directory was collected as ten explicit invocations of
`scripts/perf/repeat_profile.py --build-dir ...` (alternating parent/head), then
graded reproducibly with the same command plus `--summarize-existing`.
