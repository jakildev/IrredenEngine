# Captured finite-quad hierarchy experiment

The crowded tile fallback still loses an existing floor shadow. These captures
show that its blocker survives in the GPU record pool: the loss is in the
bounded tile query/sample fallback, not missing source geometry. The new
capture-only dump allows representation experiments against those actual records.
**This PR does not repair the rendered hole or change a production shadow query.**

| Lower box only | Add 128 distant coincident casters: current bug |
|---|---|
| ![Lower box receiver detail](lower/receiver-detail.png) | ![Missing shadow center](far-128/receiver-detail.png) |

Details are unscaled crops `(1100,600)-(1460,840)` of the retained native
2560×1440 full frames, not rendered prototype results. The distant upper caster
is visible in the full frame. Probe-on/off captures for both scenes are
pixel-identical in RGB; their commands, frame hashes and successful comparison
are in [captures.json](captures.json). No shadow reference is re-blessed.

## Method and measured work

The offline tree retains all captured records, including duplicates. Median
splits use the longest U/V/depth extent, leaves hold at most eight IDs, and queries
reject closed U/V bounds and disjoint open depth intervals before testing exact
finite quads. Every answer is compared with a complete pool scan. Both paths
short-circuit on a blocker, so work counts depend on record/traversal order.
Rational arithmetic evaluates the captured binary32 inputs exactly; independent
ray/triangle tests check the quad oracle. No shader, GPU traversal, production
allocation or dispatch is added.

Each scene uses a 17×17 sun-space grid over U,V = [-12,12], depth 2, throw 64,
bias 1/256. All **2,023** queries agree with the pool oracle. Under downward
light this is the floor plane; the oblique cases are abstract constant-sun-depth
query planes, **not** a pixel or floor-coverage comparison. All five downward
cases have the identical 65 blocked grid samples, despite the native hole.

| Capture | Records | Unique records | Node visits mean / max | Quad tests mean / max | Blocked / 289 |
|---|---:|---:|---:|---:|---:|
| lower | 1708 | 1708 | 12.94 / 27 | 2.98 / 19 | 65 |
| far-8 | 1924 | 1735 | 16.52 / 29 | 7.09 / 22 | 65 |
| far-16 | 2140 | 1735 | 11.62 / 31 | 2.13 / 16 | 65 |
| far-128 | 5164 | 1735 | 17.02 / 37 | 1.79 / 18 | 65 |
| near-128 | 5164 | 1735 | 16.25 / 51 | 2.78 / 67 | 65 |
| oblique-lower | 2220 | 2220 | 19.97 / 53 | 5.61 / 36 | 189 |
| oblique-far-128 | 5676 | 2247 | 24.72 / 51 | 5.87 / 22 | 189 |

The far-128 tree has 2,047 nodes and keeps all 5,164 records. A hypothetical
32-byte node plus 4-byte leaf-ID layout would add 86,160 bytes, excluding build
scratch, sort keys, counters, allocator and alignment. Its existing record
payload is 206,560 bytes. These are layout estimates, not allocated GPU memory.
The CSV probe reads at most the existing record capacity; the ordinary tile-only
probe does not read this payload.

This is not a bound on all queries: the rejection-heavy synthetic test has 512
coincident diagonal quad bounds covering a receiver that misses every finite
face, and it necessarily performs all 512 tests. Identical geometry is not
deduplicated here. The record counts also reveal substantial fixture duplication;
separately evaluate unique overlapping geometry before choosing a production tree.

## Capture provenance and replay

- macOS 26.5.2 arm64, Apple M4 Max, Metal, Debug. No performance timing claim.
- Runtime source: `e5fce0efa` plus [source.patch](source.patch). Binary SHA-256 is
  in `captures.json`; later offline probe/report edits do not change that binary.
- IRCanvasStress: one 18×6×8 rigid source-face box, a 40×40 GRID floor and
  0/8/16/128 coincident 3³ rigid source casters, rotation 0.47 about (1,1,1).
  ECS total was not instrumented in this capture. One sun plus ambient sky;
  no authored local lights, shadows enabled, AO disabled.
- Camera yaw 0, zoom 1, subdivision 1, origin pivot; no spin or auto-rotation.
  Game/window logical size 1280×720; native framebuffer 2560×1440, offscreen.
- Downward sun (0,0,-1); oblique sun input (0.3,0.4,-1), normalized by the engine.
  Upper offset is (0,0,-60) for distant cases, zero for nearby; exact commands
  and per-shot index counters are in the manifest. Every run reports CLEAN.
- Seven geometry captures plus two no-probe controls. Compressed CSVs retain both
  cascades' tile rows and every allocated ten-word record. Capture readbacks
  synchronize the GPU and must not be used as timing runs.

```bash
ir-acquire gpu -- python3 docs/pr-screenshots/codex/sun-geometry-capture/capture.py --output /tmp/sun-geometry-replay
python3 scripts/perf/sun_geometry_probe.py docs/pr-screenshots/codex/sun-geometry-capture/far-128/index.csv.gz --grid -12 -12 12 12 --depth 2 --steps 17
python3 scripts/tests/test_sun_geometry_probe.py
python3 scripts/tests/test_sun_face_index_probe.py
fleet-run --timeout 120 IrredenEngineTest --gtest_filter='SunShadowProbeTest.*:*SourceFaceProbeReadsPendingGpuClearAndTileBoundaries'
```

The native four-test readback suite passes, including pending GPU clear,
zero/one/exhausted record counts, exact unsigned payloads, default-off export and
inaccessible/inactive probes. Nine geometry tests and fifteen tile-parser tests
pass. Header conventions, Python lint, comment-reference lint and formatting pass.
The four-shot native mode-switch and three-shot detach sequences also export
every shot and exit CLEAN.
Independent simplify reviews found no blocker; capture callback composition was
fixed so mode-switch and detach probes can coexist with geometry export.

## Limits and next decision

The pool is complete for admitted indexed records, not all scene geometry: faces
outside both cascade domains are not recorded. Analytic-solid payloads and
exhausted record pools are rejected by the offline tool. Shader boundary
rounding, degenerate-face thresholds, receiver reconstruction, cascade selection,
non-indexed sampled layers and GPU build/publication remain outside this oracle.
Native Windows/OpenGL is unvalidated here.

Next prototype conservative GPU bounds and construction over the retained pool,
then measure clear/build/query work and scratch memory, with unique-overlap and
rejection-heavy cases. Preserve explicit incompleteness on pool exhaustion.
Compare against these records and existing production GLSL/Metal predicates;
mathematical agreement here alone cannot certify a production GPU replacement.
The [capacity design](../../../design/sun-face-index-capacity.md#captured-finite-quad-hierarchy-experiment)
owns the representation contract and unresolved failure paths.
