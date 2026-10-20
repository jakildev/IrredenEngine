# Stack performance attribution

The historical Linux zoom-1 regression did not reproduce in the
[same-runner head/base/head diagnostic](https://github.com/jakildev/IrredenEngine/actions/runs/39493225130).
This is a bounded CI comparison, not native GPU throughput qualification.

The existing `perf-gate.yml` diagnostic rebuilt the stack head
`b0524c58c5ea70cea01bc6ec9c8408327a916520`, master
`a6fe73693b0626f6008430ff96e23e4c69a258d5`, then the same head again. All six
cells completed cleanly. The two head builds have identical executable and
shader digests. The disposable runner used Ubuntu 24.04.5, EPYC 9V74,
four available threads, software OpenGL and the quick 60-frame Debug matrix.
No baseline was published and no threshold was changed.

Steady means exclude the first 15 of 60 frames:

| Cell | Master steady mean | First head | Return head | Head-repeat spread |
|---|---:|---:|---:|---:|
| Zoom 1, full subdivisions, base 1 | 362.64 ms | 355.98 ms (−1.8%) | 375.54 ms (+3.6%) | +5.5% |
| Zoom 4, full subdivisions, base 1 | 1202.28 ms | 1193.84 ms (−0.7%) | 1192.83 ms (−0.8%) | −0.1% |

The historical gate uses all-frame means instead. Those also do not reproduce
the regression: zoom 1 is 460.84 ms on master versus 459.69 ms (−0.2%) and
383.01 ms (−16.9%) on head; zoom 4 is 1219.10 ms versus 1210.60 ms (−0.7%)
and 1208.55 ms (−0.9%). The large return-head all-frame difference disappears
in the steady series; it is not a justified rendering speedup claim.

The previous historical comparisons reported approximately 10–11% zoom-1
regressions. This paired experiment supports treating those results as
unconfirmed attribution to the stack, not evidence of a measured speedup.
The repeat spread remains material; the result does not establish zero cost.
The default fog-enabled workload also excludes the new continuous cardinal
receiver path, whose separate controls remain necessary.

`linux-pair/` retains every report, manifest, compressed log, comparison and
binary/shader provenance record from the run. `manifest.json` hashes these
files. The original failed PR workflow attempts remain available on GitHub;
one retry per affected PR follows this diagnostic rather than repeated runs
until a preferred result appears.

## Native hierarchy timing attempt

The concurrent native timing matrix completed its six scene/ordering variants
and geometry checks, but its outer benchmark wrapper returned **76 / BREACH**.
Three uncovered fleet leases and observed activity invalidated isolation.
`native-rejected/` retains all six reports and the quiet report; every timing
from this attempt is rejected. No production threshold or sorting policy is
selected from these numbers.

Qualified AC-powered native timings, overflow-only construction/routing,
mixed analytic/raster completeness and Windows/OpenGL validation remain open.
The production crowded-shadow hole is still unresolved.
