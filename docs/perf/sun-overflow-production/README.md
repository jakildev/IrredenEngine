# Production overflow hierarchy: provisional engine cost

**Historical initial integration: the large crowded-cardinal cost prompted the
[guarded early-depth follow-up](guarded-depth/README.md).** This
initial parent/head/parent matrix reports 193.66 ms average wall-frame time for
the hierarchy versus 64.73/66.14 ms on its parent. The GPU frame envelope rises
from 39.000/39.442 to 163.847 ms; `entityCanvasToFb` rises from 3.777/3.934 to
126.606 ms. The fixture restores missing shadow coverage, so the parent is not
an equal-quality alternative. That does not make this production cost acceptable.

These are **provisional diagnostics, not qualified timing**. The coordinating
session observed `ir-acquire benchmark` exit 0 and an active held quiet record,
but `IR_QUIET_REPORT_FILE` was not set: no terminal `GUARDED` report survives.
Individual runs reused the outer lease and record `quiet: unknown`. All twelve
runs exited cleanly, retained fresh reports, and recorded AC power at launch;
there is no retained end-of-run power proof. A new comparison must retain the
outer quiet report and qualification checks. Do not reinterpret the missing
record as a certified guard or an observed breach.

## Retained measurements

One 360-frame run per case per pass, ordered parent-a, head, parent-b. “Steady”
excludes the first 90 frames, retaining 270; GPU envelope and stage columns are
the report's aggregate samples, not recomputed steady-only values. All values
below are milliseconds. No confidence interval is claimed from one head run.

| Case | Pass | Wall frame mean | Steady wall mean | GPU frame envelope | GPU entityCanvasToFb |
|---|---|---:|---:|---:|---:|
| complete-cardinal | parent-a | 8.390 | 8.330 | 3.011 | 0.498 |
| complete-cardinal | head | 8.410 | 8.330 | 3.162 | 0.505 |
| complete-cardinal | parent-b | 8.480 | 8.330 | 1.655 | 0.238 |
| complete-oblique | parent-a | 8.530 | 8.330 | 4.840 | 0.297 |
| complete-oblique | head | 9.290 | 8.500 | 4.889 | 0.296 |
| complete-oblique | parent-b | 8.680 | 8.380 | 4.707 | 0.290 |
| crowded-cardinal | parent-a | 64.730 | 64.760 | 39.000 | 3.777 |
| crowded-cardinal | head | 193.660 | 193.560 | 163.847 | 126.606 |
| crowded-cardinal | parent-b | 66.140 | 65.850 | 39.442 | 3.934 |
| crowded-oblique | parent-a | 67.130 | 66.600 | 40.104 | 4.013 |
| crowded-oblique | head | 68.200 | 67.740 | 40.953 | 4.158 |
| crowded-oblique | parent-b | 67.730 | 67.220 | 40.466 | 4.089 |

The frame envelope includes inter-submission gaps, and command-buffer spans
include GPU stalls; neither is GPU busy time. Use this frame-wide accounting for
cost comparisons. Per-stage rows have partial coverage of repeated per-canvas
work and must not be summed as a complete GPU frame or treated as a full index
construction timer. The crowded fixture reports **577 entities**, many private
canvases and coincident casters; this is not a large population on one canvas.
The small complete-cardinal cases are wall-frame paced, and their parent GPU
envelopes differ materially (3.011 versus 1.655 ms), another reason to avoid
small-change performance claims from this matrix.

“Oblique” in the retained case names means **camera yaw 0.6 radians and zoom 4**;
“cardinal” means yaw 0 and zoom 1. The sun stays vertical `(0,0,-1)` in all cases.
This is neither an oblique-light experiment nor a factorial rotation/zoom test.
Complete cases add no overlapping casters; crowded cases add 128 coincident
casters at offset `(0,0,-60)`. All cases use the rigid source-box/GRID-floor
fixture, floor span 40, subdivision 1, no spin/auto-rotation, origin pivot,
AO disabled and `IR_PERAXIS_SURFACE_LIGHTING=1`. The profile route has no
screenshot, geometry probe or readback capture flags. Auto-profile enables GPU
stage timing; that instrumentation remains part of these measurements.

## Provenance and next check

Apple M4 Max, macOS/Metal, Debug; offscreen logical window/game size 1280×720.
The [driver text](driver.py.txt) and [matrix](matrix.json) retain exact commands.
Each case retains its manifest and summary plus gzip-compressed report/log;
[manifest.json](manifest.json) hashes those artifacts and their uncompressed
report/log bytes. The [parent artifact](artifacts/parent.json) is clean
`ffbcf2161820906760fce7299c035a141fe523b4`; the [head artifact](artifacts/head.json)
is clean `463ed00cbbe97b23e961eaa8150afea3b85944cf`. Configurations match; artifact
manifest differences are the binary and the intended hierarchy shader changes.
Each run's binary fingerprint agrees with its variant's archived manifest.

The [separate native visual controls](../../pr-screenshots/codex/sun-overflow-hierarchy/README.md)
establish the finite-pool coverage repair and attribute five golden mismatches
to the actual parent too. They do not qualify this cost. Investigate repeated
hidden-fragment queries in entity-canvas rendering while preserving exact visible
coverage, then repeat parent/head/parent with retained guard evidence and all
fixtures. Any early-depth experiment needs independent visual parity before a
performance conclusion. Native Windows, broader render modes, exhausted pools,
raster-only depth loss, Release throughput and million-entity targets remain open.

## Follow-up hypothesis

The [fixture-only early-depth experiment](early-depth/README.md) preserves all
eleven fixture images and suggests avoiding hidden-fragment queries is promising.
Its timings failed AC-to-battery qualification despite a retained GUARDED quiet
report. The experimental shader is not production-safe across depth bands; its
result neither fixes nor clears the production merge blocker.

## Guarded production follow-up

The implemented whole-canvas route now preserves all eleven overflow frames and
all eight paired touching/intersection images. Its separate, retained GUARDED
parent/optimized/parent matrix completed on battery power and remains provisional,
as authorized by the user. Crowded cardinal frame means are 70.60 ms with correct
overflow recovery versus 66.61/67.23 ms on the parent; crowded oblique means are
71.96 versus 68.52/68.89 ms. These are not equal-quality alternatives: the parent
loses the retained overflow shadow. The earlier 193.66 ms integration was measured
in a different provisional run and is not a same-window speedup baseline.

See the [complete follow-up](guarded-depth/README.md) and its
[all-run timing archive](guarded-depth/timing/README.md). This removes the observed
large overdraw amplification in the bounded fixture; it does not establish zero
overhead, native Windows parity, fallback-canvas scalability, Release throughput
or million-entity performance.
