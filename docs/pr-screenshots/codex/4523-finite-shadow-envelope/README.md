# Finite grazing-face shadow envelopes

A trig-derived cardinal sun can leave a box face with tiny nonzero projected
area. The closed-edge tolerance previously accepted points far beyond that
finite face; a correct index consequently had to populate every tile. The
receiver now checks a finite projected envelope before inverse coordinates.
The producer encloses that envelope with independently rounded arithmetic.
See the [derivation](../../../design/sun-face-index-coverage.md).

No angular rejection threshold, filtering, shadow softness, capacity, record
layout or depth interpolation changed. The existing determinant threshold
remains. This preserves represented projected geometry within the documented
numerical domain; it does not prove arbitrary upstream transform accuracy.

## Native index reproduction

Apple M4 Max, macOS 26.5.2, Metal Debug, 2560×1440, AC power. Both runs use
parent binary `ec245a72bcfc25e6ccc39dffb33d49a7088b212708ad73fe03b2f3c98dc1baea`.
Parent shader source is commit `5a84e577b62eae376d204970d6944a27a10ae42a`;
candidate is that source plus this PR's six shader changes. The index readback
is a diagnostic run, excluded from timing results.

```sh
fleet-run IRCanvasStress --only shadowbox,floor --probe-analytic-box \
  --analytic-box-span 10 --analytic-box-count 128 --analytic-box-step 0.125 0 0 \
  --sun-direction -0.8660254 -0.00000007571034 -0.5 \
  --no-spin --no-auto-rotate --pivot-origin --yaw 0.785398163 \
  --zoom 1 --subdivisions 1 --no-ao --sun-face-index-probe \
  --auto-screenshot 30 --sweep-frames 1 30
```

| Counter | Parent | Candidate |
|---|---:|---:|
| Face records requested | 387 | 387 |
| Occupied tiles | 32,768 | 1,067 |
| Incomplete tiles | 32,768 | 6 |
| Spill nodes requested | 1,445,840 | 3,198 |

The six remaining incomplete tiles contain genuine heavy overlap; bounded
fallback remains necessary. Requested spill counts can exceed pool capacity.
Tile count sums are not insertion counts because incomplete marking uses
atomic maximum. Raw CSVs are retained compressed beside `index-summary.json`.

| Parent | Candidate |
|---|---|
| ![Parent grazing boxes](parent.png) | ![Candidate grazing boxes](candidate.png) |

The pair is RGB-identical. It checks visual preservation, not the correctness
of every existing floor-shadow artifact. The tiny grazing face is better
validated by geometry witnesses than by its subpixel silhouette.

## Geometry and compiler controls

- `test_render_grazing_face_index.py`: both shader adapters preserve binary32
  literals. The 128-face cardinal grid drops from 4,194,304 tile inserts to
  352–544, with all 128 eligible records retained and zero spill/incomplete
  tiles. The frozen parent C++ fixtures and output are archived here.
- Per backend: 37,632 represented face samples, including 5,880 thin-face
  samples, 42,506 in-map memberships, 768 far-point rejections and 23,520
  exact binary64 envelope witnesses. Both winding signs, translations and
  reflections are covered. Receiver-only FMA/reassociation and asymmetric
  determinant contraction remain valid. Removing the envelope, imposing an
  angular cutoff, or restoring full-cascade population fails a control.
- `render-source-face-coverage-metal.py`: 600,244 cascade queries under native
  runtime-default compilation, 287,493 accepted in-map, zero missing candidate
  bounds. The raw-AABB mutation fails 1,490 queries. `native-results.json` and
  `native-sources.tar.gz` retain results and reproducible fixture/launcher/shader
  sources; generated executables and the reproducible 38 MB fixture are omitted.
  This native probe checks containment, not concurrent atomic publication.
- Broad face suites: 87 tests pass; impulse shadow: 2 pass; detached triangular
  receiver: 1 passes. The final grazing suite also passes its three tests after
  adding the receiver arithmetic variants.

## Image controls

All 21 fixed images match the parent RGB exactly: six baked ShapeDebug, six
direct ShapeDebug and nine mixed CanvasStress frames. `regression/manifest.json`
retains commands, binary and shader hashes; `comparisons.json` records all pairs.
Duplicate PNGs reference the existing parent captures, with candidate PNG hashes
retained. Full run logs are included. Existing visible artifacts remain tracked
in the rendering worklist rather than being relabeled as fixed here.

## Timing limits

`perf/` retains every run's report, log, command, hashes, quiet-window status and
summary. Each case has three guarded fixed-pose runs, 635 GPU frames per run,
with valid GPU timestamps. No diagnostic index readback is enabled. Timings
include startup and screenshot cost; summaries retain warmup and tail data.
These Debug micro-scenes are frame-paced near 120 Hz, not a throughput or
million-entity qualification. Stage times vary markedly between runs.

| Case | Parent box-cast mean (range), ms | Candidate box-cast mean (range), ms | Parent / candidate frame mean, ms |
|---|---|---|---|
| Grazing 128 overlapping boxes | 0.910 (0.619–1.459) | 0.309 (0.218–0.476) | 8.877 / 8.817 |
| Ordinary 128 overlapping boxes | 1.010 (0.716–1.318) | 1.196 (0.921–1.488) | 8.843 / 8.900 |
| Ordinary one box | 0.133 (0.084–0.207) | 0.113 (0.100–0.131) | 8.840 / 8.830 |

The ordinary crowded result is slower in this sequence, with overlapping
ranges and drift in several unchanged stages. It does not establish zero
receiver overhead or a general speedup. The deterministic local-index work
reduction is the primary result. Rejection-heavy 64/256-candidate receiver
cost, dense local overflow, Release scaling and native OpenGL remain follow-ups.
