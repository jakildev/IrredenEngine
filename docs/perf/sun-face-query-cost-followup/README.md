# Finite-face query cost and index coverage

Deferred depth loading did not demonstrate a GPU benefit in the measured fixture.
The prototype is archived, and production query code is restored. A separate
deterministic investigation exposes an index/query boundary mismatch that must
be addressed before making candidate indexing more selective.

## Deferred depth experiment

The [prototype](prototype.patch) loads the record type and six projected XY
words, runs the unchanged finite-footprint test, and loads the three depth
words only for accepted footprints. Analytic records still load all ten words.
It preserves coordinate arithmetic, gamma7 edge tolerance, clamping, candidate
order, early exit and diagnostic-counter semantics. It changes no buffer layout,
tile membership, shadow bias or filtering.

Host tests execute both shader twins. Eighteen tests across ten suites passed;
word-level read witnesses confirm seven reads per finite miss and ten per hit
or analytic candidate. Injecting any of the three eager depth reads fails the
controls on each backend. [Bit comparison](compare_depth_bits.py) against the
parent returns identical separation bits for 20,952 boundary/exterior/grazing
cases per backend. These are source-level read counts, not GPU memory counters:
the skipped words share cache lines with the retained words, and the compiler
may already defer loads.

Native measurements: Apple M4 Max, macOS 26.5.2, Metal Debug,
2560×1440 offscreen output. Initial parent and candidate runs report AC power;
restored runs report battery power. IRShapeDebug uses four baked subjects (1,459 occupied
cells in 5,324 allocated slots), the fixture's floor/blockers and one local light,
yaw 45°, zoom/effective subdivision four, sun shadows on and AO off. Attached
surface lighting and visibility prepass are enabled. Query diagnostics and GPU
stage timers are disabled; frame GPU timestamps remain enabled. Each arm runs
600 frames three times; steady wall statistics exclude the first 150 frames.

| Measurement | Initial parent mean (range), ms | Candidate mean (range), ms | Restored mean (range), ms |
|---|---:|---:|---:|
| GPU frame envelope | 6.067 (5.969–6.203) | 6.254 (6.144–6.353) | 5.479 (5.371–5.571) |
| Steady wall frame | 17.113 (16.100–18.950) | 15.937 (15.620–16.230) | 16.723 (15.290–17.990) |

The candidate does not improve GPU time. Wall-time movement does not establish a
query speedup. Sequential arm ordering, the restored arm’s power-source change and host
variation prevent assigning a
reliable regression magnitude. All nine runs have guarded resource coordination,
600 valid GPU samples, fixed yaw/zoom, 403 peak overflow entries and zero drops.
GPU envelopes include submission gaps; they are not exclusive shader busy time.
This small Debug fixture does not establish Release or population performance.

The initial parent used the previously built executable. Rebuilding changed its
binary fingerprint; **candidate and restored arms use the identical rebuilt
binary**, runtime scripts and configuration. Initial and restored staged shader
sources differ only in indentation of two diagnostic counter increments in each
query-layout twin. The exact staged hashes and configuration/binary fingerprints
are retained. The final include-guard naming cleanup is mechanical and postdates
the timed runs. No timing improvement is claimed for it.

[Archives](manifest.json) retain every run's report, log, configuration fingerprints and
environment manifest, with archive/member SHA-256 hashes. The pre-experiment
source is `91cb430562b62cfb681b0065d1ff21291fd37555`; dirty-source status during
the initial arm reflects the parallel prototype edit, while the native process
used the unchanged staged parent shaders. No assets were staged during a run.

All six candidate images and six restored images are RGB-identical to their
initial-parent counterparts. [Full frames and comparisons](../../pr-screenshots/codex/sun-face-query-cost-followup/README.md)
are appearance-parity evidence, not proof that existing shadows are correct.
Native OpenGL was not run for this experiment.

To reproduce the rejected prototype in a dedicated worktree of this branch:

```bash
git apply docs/perf/sun-face-query-cost-followup/prototype.patch
python3 scripts/tests/test_render_sun_query_stats.py
python3 docs/perf/sun-face-query-cost-followup/compare_depth_bits.py
fleet-build --target IRShapeDebug
```

Use the exact environment and command retained in each profile manifest for
timing. Do not interpret diagnostic-enabled runs as performance evidence.

## Diagnosed index/query boundary mismatch — unresolved

`indexSourceSunFace` inserts an unexpanded projected AABB into tiles, while
`sourceFaceRaySeparation` accepts a conservative rounded closed footprint. The
two contracts disagree at a tile boundary even for an axis-aligned unit face:

| Input / result | Value |
|---|---|
| Face corner | `(nextafter(8,+∞), 0, 0)` |
| Face edges | `(1,0,0)` and `(0,1,0)` |
| Receiver | `(nextafter(8,−∞), .5)`, sun depth 2 |
| Near-cascade origin / texel size | `(0,0)` / `(1,1)` |
| Exact query | Accepts; separation 2 |
| Published face tile | X = 1 |
| Receiver tile | X = 0; empty, marked complete |

[The reproducer](reproduce_index_boundary.py) executes the actual production
indexer with serial host atomics, recovers its stored geometry, executes the
actual face query, and uses the receiver's tile-selection expression. Both
backends reproduce the omission on the parent and on the prototype. It is not
a rendered reproduction and does not establish the cause of earlier screenshots.

```bash
python3 docs/perf/sun-face-query-cost-followup/reproduce_index_boundary.py --repo .
```

Exit zero means the documented mismatch was reproduced, **not** a correctness
pass. This diagnostic is intentionally outside the CI test suite. Once fixed,
turn it into a regression gate asserting that every accepted receiver's tile
contains the face or is marked incomplete. Extend coverage across winding,
translations, anisotropic cascade texels, subnormal/rounded edges and grazing
bases. Derive outward-rounded producer bounds from the complete accepted query
envelope and tile mapping. Arbitrary padding, smaller tiles, or geometric SAT
alone do not prove this inclusion. After that contract is established, measure
more selective lists against build cost, memory and incomplete-list frequency.
