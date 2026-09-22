# Rejected SDF surface-query reuse experiment

## Decision

Do not ship this experiment. The measured control was the initial
[ownership implementation](../sdf-winner-ownership/README.md), commit
`65709fc555cfe96825386d2be65c507ca1a13708`. The adjacent patch retains the
experiment and its scalar mutation tests for reproduction, not as active code.
It passed four scalar test cases before removal; the replay test replaces the
SDF query with an injected signed result and does not prove the SDF solver.

The experiment computes `surfaceD` only in pass zero, stores one 32-bit result
per dispatched tile lane after the canvas owner words, and reloads it in owner,
color and non-box shadow passes. The same rejection tests and immutable canvas
inputs precede every access. A storage barrier follows pass zero. Misses are
written explicitly; the scratch tail is not cleared. Owner arbitration is
unchanged. This requires up to 64 MiB beyond the owner canvas at the 262144-tile
cap (64 lanes × four bytes per tile), allocated at the observed high-water mark.

## Timing evidence

Apple M4 Max, native Metal. Each run completed cleanly after 245 frames with
244 valid shape-stage GPU samples. `shapePass1` covers the entire shape system;
these measurements do not isolate the SDF query or election dispatch. Raw
reports retain startup outliers and timestamp rejection context where reported.

| Workload | Parent ownership GPU average ms | Reuse GPU average ms |
|---|---:|---:|
| Coincident boxes + floor, earlier parent runs ([1](../sdf-winner-ownership/after-profile-1.txt), [2](../sdf-winner-ownership/after-profile-2.txt)) | 1.982 / 1.965 | 2.125 / 1.884 |
| Coincident boxes + floor, parent rerun after experiment | 2.112 | same two runs above |
| Analytic sphere + floor + two shadow-only canvases | 2.846 | 2.718 |

The box result is variable; the single sphere pair improves about 4.5%, which
is insufficient evidence to accept the added storage and complexity. This is
not a claim that reuse cannot help a different SDF workload. Next measure
separate depth/election/publication scopes, fixed dispatch costs and warmed
per-pose distributions before selecting another optimization. Avoid attributing
the parent ownership regression entirely to repeated SDF evaluation.

## Visual evidence

Exact full-frame RGB comparisons have zero differing pixels:

| Reuse | Unmodified ownership | Pose |
|---|---|---|
| 2100 | 2110 | Boxes, yaw 0 |
| 2101 | 2111 | Boxes, yaw 45° |
| 2106 | 2108 | Spheres/shadows, yaw 0 |
| 2107 | 2109 | Spheres/shadows, yaw 45° |

Reuse:

![Reuse sphere](capture-2106.png)

Unmodified ownership:

![Parent sphere](capture-2108.png)

The sphere still has visible surface speckling. Equality proves preservation,
not correctness of that artifact. The two additional canvases intentionally
cast shadows without displaying their geometry. Investigate sphere receiver
reconstruction/self-shadowing separately; do not hide it with filtering.

## Reproduction

Apply `experiment.patch` to that historical control commit, not the current
compile-specialized shader layout, then build
`fleet-build --target IRCanvasStress -j 3`. Restore the parent and rebuild for
the control. The test addition runs with
`python3 scripts/tests/test_render_sdf_winner.py` while the patch is applied.

```sh
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-box --probe-sdf-depth-tie --pivot-origin --no-spin --no-auto-rotate --no-shadows --no-ao --subdivisions 1 --zoom 4 --auto-profile --auto-screenshot 120 --sweep-yaw 0 0.78539816 2
fleet-run --timeout 60 IRCanvasStress --only shadowbox,floor --probe-analytic-sphere --probe-analytic-canvases 3 --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 3 --zoom 2.5 --auto-profile --auto-screenshot 120 --sweep-yaw 0 0.78539816 2
```

A separate `--only orbit` attempt was rejected as a performance probe: it
submitted no SDF shapes (`ShapesToTrixel` tick count zero). Those captures are
not evidence for this optimization. All production edits and test additions
were restored after the experiment and the native target rebuilt successfully.
