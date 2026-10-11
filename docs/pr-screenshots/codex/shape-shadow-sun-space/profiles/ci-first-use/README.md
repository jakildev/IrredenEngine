# CI first-frame cost

[Perf Gate run 38161324575](https://github.com/jakildev/IrredenEngine/actions/runs/38161324575)
failed on the full 60-frame mean at zoom 1. The head was PR commit
`fe9dfc6f01a96da441c3102f9ac317cbcac4b3c5`, built as merge commit
`f821a263e73288f7cab7089917b97838e6d679ac`. The selected baseline capture
`perf-baseline@df9a13e5d3c77b34dd3b8beb627bf4a5d9584dfc` measured engine commit
`cf1d70e10bb83a9d99ad37ec2996f0842d73c881`.

The unmodified reports and manifests are in [baseline](baseline/) and
[head](head/); [provenance.json](provenance.json) records sources, full commit
identities, command, and file hashes. Both captures used the hosted Linux
OpenGL/Xvfb Mesa lane described by the workflow as llvmpipe. The manifests
report GPU model `unknown`; no renderer string or compiler stack was captured.

## Decomposition

All timings below are milliseconds. Remaining-frame means and totals use the
ordered series; reported full and steady means are rounded by the report writer.

| Measurement | Baseline | Head | Change |
|---|---:|---:|---:|
| Zoom 1, full 60-frame mean | 545.63 | 634.49 | +16.29% |
| Zoom 1, first frame | 7880.404 | 12852.122 | +4971.718 ms |
| Zoom 1, remaining 59-frame mean | 421.315 | 427.414 | +1.45% |
| Zoom 1, steady 45-frame mean | 421.24 | 426.69 | +1.29% |
| Zoom 4, full 60-frame mean | 1635.40 | 1639.79 | +0.27% |
| Zoom 4, first frame | 2884.572 | 2896.547 | +11.975 ms |
| Zoom 4, steady 45-frame mean | 1613.97 | 1617.10 | +0.19% |

Zoom 1's total excess is 5331.543 ms; its first frame accounts for 4971.718 ms
(93.25%). CPU `LightingToTrixel` maximum rises 1640.016→4263.461 ms and
`ComputeSunShadow` maximum rises 1069.085→3516.948 ms. Their combined maximum
increase is 5071.308 ms. System aggregates do not identify which frame held
each maximum, so this is supporting evidence rather than an exact frame trace.

The GPU `computeLightVolume` average rises 50.207→94.554 ms, but its maximum
rises 1871.241→4483.762 ms across 59 samples. That maximum increase accounts
for 44.280 ms of the 44.347 ms average increase. Removing one maximum from
each aggregate gives approximately 18.810→18.878 ms for the other 58 samples.
These timestamp intervals alone cannot identify compiler work.

## Contract and limits

`check_regression.py::_regressed_cells` gates the full `frame.avg`, including
startup, at **greater than or equal to 10%**. Class-matched history mode uses
raw means. The 15-frame warm-up exclusion feeds the separate steady report,
not the gate. `profile_report.cpp::computePercentiles` selects index 59 for
p99 at 60 samples, so p99 is the maximum here: the first frame in both runs.
The gate correctly fails; the decomposition does not make it a pass.

The newly reachable curved-shape query graph in `ir_sun_shadow_sample` is
shared by the two CPU systems with large maxima. The distribution supports a
first-use cost, including shader JIT as a hypothesis. It does **not** prove
JIT attribution. The captures are separate hosted runs, not a controlled
same-runner cold A/B. Calibration is close (baseline 103.957 ms; head
103.633 ms), but that does not prove identical load throughout either run.
The reports do not establish hardware OpenGL or Metal performance. Confirm
the cause and any fix with controlled cold captures or a compiler stack trace;
do not remove startup from the gate to hide this cost.

## Reproduce

From the repository root, this flat capture layout reproduces the raw failing
comparison against the already-selected baseline (expected exit status 1):

```bash
python3 scripts/perf/check_regression.py docs/pr-screenshots/codex/shape-shadow-sun-space/profiles/ci-first-use/baseline docs/pr-screenshots/codex/shape-shadow-sun-space/profiles/ci-first-use/head
```

It does not rerun historical baseline selection. Recompute the frame
decomposition with the production parser:

```python
import statistics
import sys
from pathlib import Path

sys.path.insert(0, "scripts/perf")
from compare_perf_runs import parse_report

root = Path("docs/pr-screenshots/codex/shape-shadow-sun-space/profiles/ci-first-use")
for zoom in (1, 4):
    name = f"target=IRPerfGrid,zoom={zoom},sub_mode=full,sub_base=1.txt"
    rows = [parse_report(root / side / name, name) for side in ("baseline", "head")]
    for side, row in zip(("baseline", "head"), rows):
        frames = row.frame_times_ms
        print(zoom, side, "first", frames[0], "remaining mean",
              statistics.mean(frames[1:]), "steady", row.steady_frame.avg)
    base, head = (row.frame_times_ms for row in rows)
    excess = sum(head) - sum(base)
    first = head[0] - base[0]
    print("total excess", excess, "first excess", first, "share", first / excess)
```
