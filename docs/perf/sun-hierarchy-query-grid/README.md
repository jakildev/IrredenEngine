# Distinct receiver-grid controls

The hierarchy diagnostic can vary receiver density with `--query-side` while
preserving the existing 17×17 default. A 257×257 grid evaluates 66,049 distinct
positions; repeating 289 positions does not establish the same workload.
Both modes remain synthetic sun-UV planes, not actual screen receivers.

Native Metal validation uses the oblique-far-128 captured pool (5,676 records),
both record orders and both math modes. All 532,748 hierarchy/exhaustive query
comparisons match: 1,089 × four unsorted controls, plus 66,049 × eight
sorted/unsorted controls. The dense variants also agree with each other at
every position, with 44,767 blocked positions. No timing was requested.

| Dense variant | Record order | Mean face tests | Maximum face tests |
|---|---|---:|---:|
| Unsorted | Captured | 55.84 | 128 |
| Unsorted | Reversed | 93.81 | 232 |
| Spatial | Captured or reversed | 21.20 | 86 |

The default point sequence is byte-equivalent to the prior input. CPU tests
check dense sample uniqueness after binary32 packing, row ordering, endpoints,
spacing, repetition and admission limits before native work. The maximum
1,048,576-query input uses 16 MiB for points and 16 MiB for GPU outputs, plus
the existing tree/record buffers; host validation and JSON storage are larger.

The three compressed reports retain source/input hashes, command, device,
compiler, record-order coverage and counters. `manifest.json` hashes each
report. They establish dense-query correctness against the exhaustive GPU
predicate; candidate counts do not establish a speedup or full-frame cost.

```bash
ir-acquire gpu -- python3 scripts/perf/sun_hierarchy_probe.py \
  docs/pr-screenshots/codex/sun-geometry-capture/oblique-far-128/index.csv.gz \
  --query-side 257 --spatial --output /tmp/sun-grid-257
```

Omit `--spatial` for the matching unsorted control. Timings additionally require
`--measure` and an intact `ir-acquire benchmark` quiet window. The remaining
work is qualified timing, actual surface receiver workloads, and overflow-only
production construction/query routing with mixed-record completeness.
