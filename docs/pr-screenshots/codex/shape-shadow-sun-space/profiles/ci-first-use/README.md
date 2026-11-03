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

## Cascade-call consolidation

[Run 38163741822](https://github.com/jakildev/IrredenEngine/actions/runs/38163741822)
passes the unchanged gate after consolidating five query call sites into one.
Its PR head is `3fe9f6cd6477976473858bda30bd0d9d2b8c73c7`, captured as merge
`356583fac111236ff49aaa1f22393fb8a26ede18`. [Raw results](cascade-loop/) and
[provenance](cascade-loop-provenance.json) retain the reports and hashes.
The selected baseline is the same `df9a13e5d` capture above, with close
calibration (103.95 versus 103.96 ms).

| Measurement | Master baseline | Before consolidation | After consolidation |
|---|---:|---:|---:|
| Zoom 1 full mean | 545.63 | 634.49 | 533.67 |
| Zoom 1 first frame | 7880.404 | 12852.122 | 6981.058 |
| Zoom 1 steady mean | 421.24 | 426.69 | 424.88 |
| Zoom 4 full mean | 1635.40 | 1639.79 | 1642.96 |
| Zoom 4 steady mean | 1613.97 | 1617.10 | 1622.53 |

The full means are now -2.19% and +0.46% relative to master. These are separate
class-matched hosted runs; the result removes the observed gate failure but does
not isolate compiler work or prove a hardware GPU speedup. Zoom 4 CPU
`ComputeSunShadow` remains higher (5.874→7.220 ms), including the minimum and
the mean excluding the maximum; its GPU stage is nearly flat (10.497→10.545 ms).
Do not describe every stage as unchanged.

## Same-runner head/base/head

[Diagnostic run 38163751570](https://github.com/jakildev/IrredenEngine/actions/runs/38163751570)
captured new head, previous PR head, then new head again on one
`linux-x86_64-epyc-9v74-80-unknown` runner. It did not publish a baseline.
[Raw reports, manifests, comparisons and binary/shader hashes](same-runner/)
retain all three arms. The binaries match; the two new-head shader hashes
match each other and differ from the previous-head shaders.

| Measurement, ms | New head first | Previous head | New head return |
|---|---:|---:|---:|
| Zoom 1 full mean | 428.89 | 435.33 | 379.54 |
| Zoom 1 first frame | 4324.679 | 5612.153 | 1360.089 |
| Zoom 1 steady mean | 343.15 | 346.81 | 369.28 |
| Zoom 1 remaining 59-frame mean | 362.856 | 347.590 | 362.923 |
| Zoom 4 full mean | 1184.15 | 1189.77 | 1188.31 |
| Zoom 4 steady mean | 1167.35 | 1172.67 | 1171.32 |

Both new-head full-window means are below the previous head. This diagnostic
workflow completes comparisons without applying the performance gate; the
separate PR run above is the gate pass. However,
shader caches are not cleared between arms: unchanged shaders can be warm in
the previous-head arm, and the return head can reuse its first arm's compiled
shaders. The large return-head first-frame gain is not a cold-start result.
Zoom 1 steady time varies by 7.6% between the identical new-head arms; its
return is 6.5% above the previous head. Both new-head remaining-frame means are
about 4.4% higher. The first head has 13 slow frames at indices 2–14, excluded
by the official warm-up; the return has 14 at indices 21–34, included in its
steady statistic. Do not select the more favorable warm-up result as a speedup.
Zoom 1 GPU `computeSunShadow` rises 14.112→16.383/16.682 ms (16–18%),
including higher minima. Zoom 4 CPU `ComputeSunShadow` rises
4.911→5.247/5.328 ms while its GPU stage falls 15.76→15.08/15.02 ms.
These runs support reduced first-use cost, with unresolved runtime costs, not
a stable throughput gain or full stage parity. Attribute the repeatable stage
costs and frame bursts before claiming performance neutrality; dense native
profiling and hardware GPU validation remain open.

## Reproduce comparisons

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
