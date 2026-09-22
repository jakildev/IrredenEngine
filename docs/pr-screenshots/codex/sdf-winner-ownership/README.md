# Deterministic opaque SDF ownership

Native Metal captures on Apple M4 Max, 2560×1440. Parent production code:
`479964b7a67d572b47ce22ab575101bcbf540930`, with the new overlap fixture added.
The fixture submits coincident blue and orange analytic boxes. Blue is first
in the fixed submission order and must own every winning opaque sample.

## Results

| Capture group | Configuration | Observation |
|---|---|---|
| 2079–2082 | Ownership enabled; yaw 0/90/180/270° | Blue wins throughout; zero orange pixels in the color segmentation |
| 2083–2086 | Negative control: remove only the Metal publication owner predicate | Orange wins almost everywhere, with stray blue pixels at two angles |
| 2087–2091 | Ownership enabled; three analytic canvases, subdivision 3; yaw 0/22.5/45/67.5/90° | Clean exit; displayed main box remains blue. Smoke coverage, not an oracle for every auxiliary surface |
| 2092–2093 | Actual parent production implementation; yaw 0/45° | Baseline for the paired profile recipe |
| 2096–2097 | Ownership implementation; same recipe | Deterministic blue winner |

The negative control is a deliberate mutation, not the untouched parent.
Opaque color/identity publication has one writer for a fixed tile submission
order. Reordering descriptors can change the tie winner. X-ray blending is
outside this guarantee. This change does not yet retain receiver geometry or
fix projected shadow edges.

Parent:

![Parent overlap](capture-2092.png)

Ownership enabled:

![Deterministic winner](capture-2096.png)

## Reproduce

Build with `fleet-build --target IRCanvasStress -j 3`.

```sh
fleet-run --timeout 45 IRCanvasStress --only shadowbox --probe-analytic-box --probe-sdf-depth-tie --pivot-origin --no-spin --no-auto-rotate --no-shadows --no-ao --subdivisions 1 --zoom 4 --auto-screenshot 6 --sweep-yaw 0 4.71238898 4
fleet-run --timeout 45 IRCanvasStress --only shadowbox --probe-analytic-box --probe-sdf-depth-tie --probe-analytic-canvases 3 --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --auto-screenshot 6 --sweep-yaw 0 1.57079633 5
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-box --probe-sdf-depth-tie --pivot-origin --no-spin --no-auto-rotate --no-shadows --no-ao --subdivisions 1 --zoom 4 --auto-profile --auto-screenshot 120 --sweep-yaw 0 0.78539816 2
```

Do not add `--no-lighting`: the current demo pipeline then omits shape
rasterization. An initial attempt with that flag was black and rejected.

## Measured cost — merge consideration

The last recipe ran twice per implementation, 245 frames per run, with 244
valid GPU shape-stage samples in each report. Raw reports are adjacent.

| Implementation/run | CPU ShapesToTrixel average ms | GPU shapePass1 average ms |
|---|---:|---:|
| Parent 1 | 0.083 | 0.942 |
| Parent 2 | 0.084 | 1.040 |
| Ownership 1 | 0.092 | 1.982 |
| Ownership 2 | 0.092 | 1.965 |

This is approximately twice the SDF-stage GPU cost in this overlap fixture,
not a performance-neutral fix. `shapePass1` measures the entire shape-system
bundle, including encoder-boundary effects, not the election kernel alone.
The reports include startup/transition outliers and rejected timestamp pairs;
these are aggregate timings, not a per-pose benchmark or a population scaling
claim. The runs were sequential on the same host.

Storage is four bytes per pixel of the largest processed SDF canvas, reused
across canvases and frames. Only canvases with SDF tiles pay the added clear
and election dispatch. Reducing repeated SDF evaluation while preserving the
single-writer guarantee is a follow-up before accepting this cost as a default.

## Validation

- Native target build, format-changed and header-checks passed.
- Rendering test runner: 31 suites passed, zero failures.
- New winner tests: three passed; extracted GLSL/Metal scalar logic rejects
  aliased owners, missing owner checks and missing depth checks. Capacity
  overflow also fails the production static assertion.
- Independent review found a buffer-clear synchronization requirement;
  an ALL barrier before the clear addresses it. Re-review found no remaining
  correctness blockers.
- Native OpenGL execution remains unverified; scalar shader tests do not
  replace Windows smoke validation.
