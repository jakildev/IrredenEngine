# Spatial finite-shadow hierarchy experiment

GPU Morton ordering improves oblique-light traversal while preserving every
tested result. It is not uniformly better: several cardinal pools need more
face tests and sorting adds build cost. Production shadow routing is unchanged;
the crowded-overlap hole remains unresolved.

## Construction and correctness

The diagnostic extends the [unsorted GPU hierarchy](../sun-hierarchy-gpu/README.md).
An initial GPU bounds tree supplies the normalization range. Face centers receive
30-bit XYZ Morton keys, with ten bits per axis. Uncertifiable or degenerate
ranges use a zero coordinate; keys only affect ordering, never visibility.
The existing Metal per-axis overflow sorter orders `(key, 0, record ID)` triples.
Only its frame-data parameter is adapted; its comparison and sorting network
are unchanged. A portable executed test compares the diagnostic's merge schedule
with the production host helper and pins its block/thread assumptions.

The final tree groups eight sorted record IDs per leaf and uses the existing
conservative bounds and finite-face predicate. Original geometry records are
neither copied nor discarded. Readbacks validate a complete permutation,
monotonic keys, sentinel padding, exact rational leaf enclosure and parent
enclosure. Every diagnostic query is compared with an exhaustive GPU query;
captured scenes additionally match the previous exact-rational oracle.

Seven scenes × 289 queries × two record orders × two math modes, plus fifteen
synthetic queries in both modes, give **8,122 matching query comparisons**.
Controls include 65,536 records, the rejection-heavy linear worst case and
scrambled XYZ octants. The octant control rejects an all-zero-key mutation,
which would pass a permutation-only test. Portable replay also rejects dropped
or duplicated IDs, reversed equal-key IDs, invalid keys, stale padding,
under-sized bounds and query disagreement.

Captured order, fast math:

| Pool | Records | Mean/max face tests: unsorted | Mean/max face tests: spatial |
|---|---:|---:|---:|
| lower | 1,708 | 1.87 / 24 | 4.52 / 16 |
| far 8 | 1,924 | 1.50 / 16 | 6.36 / 16 |
| far 16 | 2,140 | 1.70 / 22 | 6.36 / 16 |
| far 128 | 5,164 | 2.34 / 31 | 6.36 / 16 |
| near 128 | 5,164 | 2.19 / 28 | 4.54 / 23 |
| oblique lower | 2,220 | 68.67 / 120 | 15.32 / 52 |
| oblique far 128 | 5,676 | 55.41 / 127 | 20.84 / 48 |

Reversed oblique-far-128 records need 95.80 mean / 221 maximum tests without
sorting and 20.84 / 48 with sorting. Equal candidate counts for these two
sorted orders are an observation, not a universal allocation-order cost guarantee.
All visibility answers remain order-independent in the fixtures. The
512-face footprint-miss case still tests every face.

The sort buffer uses `(2 + max(2048, nextPowerOfTwo(records)) × 3) × 4` bytes:
98,312 for far-128 and 786,440 at maximum capacity. The adapter also allocates
336 bytes for the sorter's unused indirect arguments. This is additional to
the 65,536-byte / 524,288-byte hierarchy respectively and existing record/index
storage. The complete initial bounds build, key generation, padding, sort and
ordered rebuild are included in the build measurement.

## Measurement contract

Host: Apple M4 Max, macOS 26.5.2, AC power. Native launcher compiled with
Apple clang 21, `-O2`; runtime Metal shaders use fast and safe math separately.
Warmup runs for at least three seconds and two iterations. Seven measured
samples retain separate build and query GPU command-buffer spans. Each span
contains eight complete builds or eight query dispatches and is divided by
eight. Ordering between dispatches is included.

Timed shaders compile out both the exhaustive scan and traversal counters;
an executed preprocessing test verifies their absence. Each timed batch's
final output is checked against the diagnostic outside the GPU timer.
The receiver workload repeats the same 17×17 grid 256 times (73,984 queries).
It is coherent and cache-friendly; it does not represent 73,984 distinct
screen samples, a full frame, geometry production or CPU submission cost.
Repeated batches also do not measure a cold single-build frame.

Preliminary single-dispatch samples varied substantially between runs despite
successful benchmark-wrapper exits. They motivated batching, not a selected
best-run speedup claim. Keep measurement variability separate from the
deterministic reduction in face tests.

`measurements/runs.json` retains both single-dispatch matrices (opposite variant
orders) and the batched matrix, with every sample in compressed reports. The
single-dispatch runs returned zero but lack retained quiet reports and remain
exploratory. The batched run returned **76 / BREACH** because two agent leases
were uncovered; its timings are rejected. A subsequent isolated attempt
timed out waiting for the fleet GPU lock after 600 seconds and never ran.
There is **no qualified timing comparison in this evidence set**. Do not use
these reports to approve a performance gain or choose a production threshold.

## Replay

```bash
ir-acquire gpu -- python3 scripts/perf/sun_hierarchy_probe.py \
  --self-test --spatial --output /tmp/sun-spatial-controls
IR_QUIET_REPORT_FILE=/tmp/sun-spatial.quiet ir-acquire benchmark -- \
  python3 scripts/perf/sun_hierarchy_probe.py \
  docs/pr-screenshots/codex/sun-geometry-capture/oblique-far-128/index.csv.gz \
  --spatial --measure --repeat-queries 256 --output /tmp/sun-spatial-timing
python3 scripts/tests/test_sun_hierarchy_probe.py
```

Output directories must be empty. Omit `--spatial` for the unsorted comparison.
Retain the wrapper's exit code and quiet report: the report file inside the
output directory alone cannot certify the outer hold. No GPU is required to
replay the committed validation readbacks. Their manifest covers reports and
lossless compressed bounds, query results and sorted IDs. Reports preserve
input/capture/source hashes, parent revision, dirty state, compiler and device.

## Adoption boundary

Keep the complete-tile fast path. Next investigate constructing/querying this
hierarchy only when overflow requires it, with actual receiver workload and
full-frame cost. Before production use, establish analytic-record bounds,
mixed raster-only completeness, exhausted-pool behavior, engine resource
lifetimes/bindings and native OpenGL parity. Then capture moving-height,
rotation and subdivision cases against independent geometry controls.
No CPU tree construction, geometry readback, query truncation, blur or widened
shadow bias is part of the proposed production path.
