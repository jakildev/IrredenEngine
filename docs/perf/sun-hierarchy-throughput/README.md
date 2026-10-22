# Guarded distinct-grid hierarchy timings

Use the unsorted hierarchy for the first production overflow integration, while
preserving the complete-tile fast path. In these controls, spatial ordering cuts
oblique query time but usually adds about 0.14 ms of construction. It does not
clearly improve the captured oblique pool's build-plus-query total, and the
cardinal cases favor unsorted construction. Reversed oblique records benefit
from sorting overall. This is a starting implementation choice, not a universal
ordering policy or an FPS target met.

## Evidence and workload

Both outer `ir-acquire benchmark` invocations returned zero, as observed by the
coordinating session, and their retained quiet reports say **GUARDED**. All eight
complete reports are retained losslessly, including every measured sample,
visibility result, source hash, compiler version and input command. The manifest
hashes their compressed bytes and records original uncompressed hashes; it also
covers the exact driver text and quiet reports.

Host: Apple M4 Max, macOS 26.5.2 arm64, Apple clang 21.0.0, native launcher `-O2`.
Every report records AC power before and after the run, clean source revision
`5fe9e07918efb6ed32d365f63bfdcb090b732f84`, and both Metal fast and safe math.
The report key `precise` selects safe math. Source captures are the
[finite-geometry witnesses](../../pr-screenshots/codex/sun-geometry-capture/README.md):
5,164 faces for `far-128` (cardinal/downward sun), 5,676 for
`oblique-far-128`. The receiver workload is a synthetic 257×257 sun-UV plane:
66,049 distinct positions over [−12,12]² at depth 2, no repeated grid copies,
throw 64 and bias 1/256. These are not captured screen-surface receivers.

Each report evaluates captured and reversed record order in both math modes.
All 2,113,568 diagnostic comparisons (eight reports × four cases × 66,049)
match the exhaustive GPU predicate. Cardinal pools block 12,545 positions;
oblique pools block 44,767. The timed shader removes the exhaustive scan and
counters, and checks each timed batch's final visibility against its diagnostic
outside the GPU timer. This extends the
[distinct-grid correctness controls](../sun-hierarchy-query-grid/README.md).

- Run A: seven measured samples, eight dispatches per sample. Variant order:
  cardinal unsorted, cardinal spatial, oblique spatial, oblique unsorted.
- Run B: fifteen measured samples, 64 dispatches per sample, reversed variant
  order. This also changes batch size and sample count; it is a sensitivity
  repeat, not a controlled estimate of ordering effects alone.

Each case warms for at least three seconds and two iterations. Build and query
use separate GPU command-buffer timestamps, divided by dispatches per sample.
Build includes leaf/parent construction; spatial build also includes the initial
bounds tree, key generation, sentinel fill, shared sorter and ordered rebuild.
Batch ordering is included. Inputs and resources are already resident; repeated
builds and queries are hot-GPU measurements. CPU compilation, upload, submission,
readback, geometry production and full engine rendering are not measured.

## All samples summarized

Values are **median [minimum–maximum] milliseconds per dispatch**. `Sum` is the
median/range of each round's build time plus query time: an arithmetic sum of
separate timed spans, not a measured combined production dispatch. No samples
are discarded, and the runs and math modes are not pooled. `captured` and
`reversed` describe input record order, independently of the variant order above.

### Run A

| Pool | Tree | Records | Math | Build | Query | Sum |
|---|---|---|---|---:|---:|---:|
| cardinal | unsorted | captured | fast | 0.0324 [0.0300–0.0327] | 0.0344 [0.0317–0.0346] | 0.0668 [0.0616–0.0671] |
| cardinal | unsorted | captured | safe | 0.0321 [0.0250–0.0331] | 0.0339 [0.0314–0.0349] | 0.0658 [0.0564–0.0675] |
| cardinal | unsorted | reversed | fast | 0.0805 [0.0200–0.0811] | 0.0861 [0.0189–0.0862] | 0.1667 [0.0389–0.1672] |
| cardinal | unsorted | reversed | safe | 0.0711 [0.0483–0.0858] | 0.0749 [0.0485–0.0899] | 0.1481 [0.0988–0.1750] |
| cardinal | spatial | captured | fast | 0.1506 [0.1481–0.1616] | 0.0428 [0.0416–0.0435] | 0.1927 [0.1897–0.2051] |
| cardinal | spatial | captured | safe | 0.1563 [0.1556–0.1589] | 0.0439 [0.0436–0.0440] | 0.2002 [0.1996–0.2025] |
| cardinal | spatial | reversed | fast | 0.1599 [0.1565–0.1612] | 0.0435 [0.0414–0.0435] | 0.2016 [0.2001–0.2045] |
| cardinal | spatial | reversed | safe | 0.1404 [0.1399–0.1410] | 0.0408 [0.0407–0.0409] | 0.1813 [0.1808–0.1817] |
| oblique | spatial | captured | fast | 0.1603 [0.1418–0.1625] | 0.1231 [0.1205–0.1237] | 0.2833 [0.2622–0.2857] |
| oblique | spatial | captured | safe | 0.1579 [0.1572–0.1584] | 0.1254 [0.1244–0.1255] | 0.2830 [0.2824–0.2839] |
| oblique | spatial | reversed | fast | 0.1600 [0.1553–0.1614] | 0.1234 [0.1228–0.1238] | 0.2834 [0.2784–0.2851] |
| oblique | spatial | reversed | safe | 0.1557 [0.1472–0.1579] | 0.1251 [0.1245–0.1256] | 0.2813 [0.2717–0.2826] |
| oblique | unsorted | captured | fast | 0.0207 [0.0202–0.0208] | 0.2418 [0.2407–0.2422] | 0.2623 [0.2614–0.2629] |
| oblique | unsorted | captured | safe | 0.0198 [0.0196–0.0203] | 0.2420 [0.2415–0.2423] | 0.2615 [0.2611–0.2626] |
| oblique | unsorted | reversed | fast | 0.0226 [0.0223–0.0229] | 0.3907 [0.3893–0.3924] | 0.4134 [0.4122–0.4150] |
| oblique | unsorted | reversed | safe | 0.0199 [0.0197–0.0200] | 0.3906 [0.3896–0.3923] | 0.4106 [0.4094–0.4120] |

### Run B

| Pool | Tree | Records | Math | Build | Query | Sum |
|---|---|---|---|---:|---:|---:|
| oblique | unsorted | captured | fast | 0.0216 [0.0195–0.0221] | 0.2413 [0.2393–0.2423] | 0.2621 [0.2603–0.2645] |
| oblique | unsorted | captured | safe | 0.0201 [0.0195–0.0205] | 0.2430 [0.2413–0.2440] | 0.2633 [0.2607–0.2642] |
| oblique | unsorted | reversed | fast | 0.0217 [0.0194–0.0222] | 0.3898 [0.3870–0.3920] | 0.4117 [0.4064–0.4141] |
| oblique | unsorted | reversed | safe | 0.0219 [0.0200–0.0223] | 0.3955 [0.3935–0.3960] | 0.4171 [0.4136–0.4181] |
| oblique | spatial | captured | fast | 0.1589 [0.1508–0.1623] | 0.1232 [0.1210–0.1235] | 0.2816 [0.2729–0.2856] |
| oblique | spatial | captured | safe | 0.1379 [0.1373–0.1444] | 0.1223 [0.1222–0.1224] | 0.2602 [0.2595–0.2667] |
| oblique | spatial | reversed | fast | 0.1606 [0.1496–0.1618] | 0.1235 [0.1228–0.1236] | 0.2839 [0.2731–0.2852] |
| oblique | spatial | reversed | safe | 0.1567 [0.1493–0.1583] | 0.1254 [0.1224–0.1256] | 0.2814 [0.2717–0.2837] |
| cardinal | spatial | captured | fast | 0.1592 [0.1495–0.1617] | 0.0430 [0.0402–0.0434] | 0.2015 [0.1919–0.2047] |
| cardinal | spatial | captured | safe | 0.1537 [0.1396–0.1580] | 0.0432 [0.0406–0.0440] | 0.1975 [0.1804–0.2015] |
| cardinal | spatial | reversed | fast | 0.1596 [0.1463–0.1622] | 0.0434 [0.0431–0.0436] | 0.2028 [0.1894–0.2058] |
| cardinal | spatial | reversed | safe | 0.1570 [0.1429–0.1594] | 0.0437 [0.0416–0.0440] | 0.2008 [0.1867–0.2034] |
| cardinal | unsorted | captured | fast | 0.0218 [0.0194–0.0220] | 0.0266 [0.0237–0.0271] | 0.0486 [0.0431–0.0491] |
| cardinal | unsorted | captured | safe | 0.0218 [0.0211–0.0223] | 0.0272 [0.0269–0.0275] | 0.0490 [0.0484–0.0494] |
| cardinal | unsorted | reversed | fast | 0.0218 [0.0213–0.0222] | 0.0220 [0.0214–0.0222] | 0.0438 [0.0432–0.0442] |
| cardinal | unsorted | reversed | safe | 0.0197 [0.0192–0.0199] | 0.0192 [0.0191–0.0193] | 0.0389 [0.0383–0.0391] |

## Interpretation and next checks

The cardinal reversed-unsorted case is noisy in Run A: fast-math build-plus-query
spans 0.0389–0.1672 ms, versus 0.0432–0.0442 ms in Run B. Both ranges remain in the
table. In the captured oblique case, unsorted totals are about 0.262 ms; spatial
fast math is about 0.282–0.283 ms. Safe math is about 0.283 ms in Run A and
0.260 ms in Run B, so that captured-order total does not establish a consistent
sorting win. The reversed oblique case is more convincing within this workload:
unsorted totals are about 0.411–0.417 ms versus spatial 0.281–0.284 ms. Sorting
halves oblique query cost while the total result depends on construction and
record order; candidate-count reduction alone is not the decision criterion.

Next integrate the unsorted tree only for overflow, retaining all blockers and
the complete-tile path. Establish mixed finite/analytic/raster completeness,
record-pool exhaustion handling, resource lifetime and query/build scheduling.
Then validate moving heights, rotations, subdivisions and actual surface
receivers on Metal and native Windows/OpenGL. Measure full scenes before
choosing an optional sort threshold. Revisit million-entity performance only
with explicit visible geometry, canvases, lighting, resolution and frame-time
budgets; these 5,164/5,676-face microbenchmarks establish none of that target.
The production crowded-shadow hole remains unresolved.

## Reproduce and inspect

From the dedicated worktree root, the retained drivers reproduce the two variant
orders. Their output paths under `/tmp` must be empty before reuse; edit those
paths in a copy when retaining another experiment. The wrapper must finish with
exit zero and a `GUARDED` quiet report before using new timing evidence.

```bash
IR_QUIET_REPORT_FILE=/tmp/shadow-distinct-qualified-a.quiet ir-acquire benchmark -- \
  python3 docs/perf/sun-hierarchy-throughput/run-a/driver.py.txt
IR_QUIET_REPORT_FILE=/tmp/shadow-distinct-qualified-b.quiet ir-acquire benchmark -- \
  python3 docs/perf/sun-hierarchy-throughput/run-b/driver.py.txt
```

For one cell, use `scripts/perf/sun_hierarchy_probe.py` with a retained capture,
`--query-side 257 --measure`, and `--spatial` for sorted construction. Run B adds
`--rounds 15 --dispatches 64`. The previous
[rejected timing attempts](../sun-spatial-hierarchy/README.md#measurement-contract)
remain rejected; this evidence does not replace or relabel those reports.
