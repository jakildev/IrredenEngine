# GPU finite-quad hierarchy diagnostic

This experiment builds and queries a conservative hierarchy on native Metal.
Production rendering is unchanged: the crowded shadow hole remains open.
It reuses the seven [captured geometry pools](../../pr-screenshots/codex/sun-geometry-capture/README.md)
and the production `sourceFaceIndexBounds` and `sourceFaceRaySeparation` helpers.
No CPU-built topology, sorting, or per-frame geometry readback is proposed for
the engine. The diagnostic uploads previously captured records to isolate the
GPU construction and query contract.

## Representation and checks

`scripts/perf/sun_hierarchy_probe.py` assigns eight consecutive records to each
leaf of a complete binary heap. Leaves are padded to a power of two; parents
merge bounds in separate, ordered Metal compute encoders. All populated nodes
are overwritten, so no separate tree-clear dispatch is needed. The default
tracked resources establish publication between encoders. Traversal uses heap
indices to ascend to the next sibling, without a per-thread stack or a work
budget that could silently truncate a query.

A leaf stores conservative U/V/depth bounds. The existing projected-bound
helper also encloses the affine depth coordinate: receiver coefficients are
clamped to [0,1]. Bounds that cannot be certified become unbounded, propagate
through parents, and cannot reject a query. Empty leaves are distinct from
unbounded leaves. Closed U/V bounds and outward-rounded separation intervals
only eliminate candidates; the production finite-face predicate decides hits.

Every query also executes an exhaustive scan with the same production
predicate. Host readback independently checks exact rational enclosure of every
quad, parent/child enclosure, empty/unbounded flags, publication and counters.
The seven scenes additionally match the previous exact-rational oracle's full
coverage arrays. Nine synthetic cases check known visibility: empty pool,
first/last lower blocker with 512 distant duplicates, distant-only pool,
65,536-record capacity, rejection-heavy footprint misses, closed face edges,
strict bias/throw endpoints and adjacent binary32 values, subnormal inputs,
and reversed face orientation.

Native validation runs both `MTLMathModeFast` and `MTLMathModeSafe`. All seven
captures are also tested with reversed record order. There are **8,120 native
query comparisons with zero mismatches**: 7 scenes × 289 queries × 2 orders ×
2 math modes, plus 14 synthetic queries × 2 math modes. This is finite fixture
coverage, not a proof for every binary32 input or driver.

## Observations

Host: Apple M4 Max, macOS 26.5.2, Metal. SDK support for `MTLMathMode` is required.
There are **no timing or FPS measurements** here. The diagnostic executes the
exhaustive reference in the query kernel, so its duration must not be treated
as accelerated-query cost. No native Windows/OpenGL qualification is claimed.

Captured order, fast math (safe mode produces the same observed results):

| Pool | Records | Mean/max node visits | Mean/max finite-face tests |
|---|---:|---:|---:|
| lower | 1,708 | 11.13 / 22 | 1.87 / 24 |
| far 8 | 1,924 | 11.13 / 32 | 1.50 / 16 |
| far 16 | 2,140 | 13.08 / 39 | 1.70 / 22 |
| far 128 | 5,164 | 16.65 / 47 | 2.34 / 31 |
| near 128 | 5,164 | 14.50 / 47 | 2.19 / 28 |
| oblique lower | 2,220 | 163.89 / 239 | 68.67 / 120 |
| oblique far 128 | 5,676 | 155.27 / 246 | 55.41 / 127 |

All downward pools preserve the same 65 blocked queries. Both oblique pools
preserve 189, but their plane is constant sun depth, **not a floor-pixel
comparison**. Reversing records preserves coverage while changing traversal
cost: oblique far 128 rises to 95.80 mean / 221 maximum face tests. The unsorted
tree therefore does not reproduce the offline spatial median tree's efficiency.
GPU spatial ordering is the next candidate to evaluate, together with its
construction/scratch cost. Do not adopt this unsorted tree on candidate counts
alone.

The 512-face footprint-miss control visits all 512 faces and 127 nodes. The
maximum-capacity lower-blocker control visits 27 nodes and eight faces. These
controls establish both useful depth rejection and the unavoidable linear
worst case. Coincident duplicates are retained, not optimized away.

The far-128 tree allocates 65,536 bytes (including unused node zero), compared
with 206,560 bytes of source record payload. Maximum-capacity tree storage is
524,288 bytes. These figures exclude any future spatial keys/sort scratch and
do not replace or subtract the existing production index allocation.

## Replay and provenance

```bash
ir-acquire gpu -- python3 scripts/perf/sun_hierarchy_probe.py \
  docs/pr-screenshots/codex/sun-geometry-capture/far-128/index.csv.gz \
  --output /tmp/sun-hierarchy-far128
ir-acquire gpu -- python3 scripts/perf/sun_hierarchy_probe.py \
  --self-test --output /tmp/sun-hierarchy-controls-new
python3 scripts/tests/test_sun_hierarchy_probe.py
```

Each output directory must be empty. `results.json` records the source parent
revision and dirty state, compiler, script/expanded-shader/launcher/binary
hashes, input hash, capture hash and both math modes' full coverage/counters.
These runs used parent `a36839cdcf211394208df6014bcf748b38b5b882` plus the new
probe script in this change. The committed compressed query and bounds
readbacks are lossless; `manifest.json` hashes them. The host test reconstructs
each input, replays every readback check, and compares each capture's result to
the preceding exact oracle. No GPU is needed for that replay. Native generated
executables are not committed.

The [spatial-ordering follow-up](../sun-spatial-hierarchy/README.md) evaluates
GPU sorting and separately timed build/query paths with reference work disabled.
Remaining before production adoption: mixed analytic/raster-only
layers; exhausted record-pool behavior; engine dispatch/lifetime integration;
native OpenGL parity; moving-height and full-frame visual checks. The current
record pool only represents geometry admitted by its cascade domains.
