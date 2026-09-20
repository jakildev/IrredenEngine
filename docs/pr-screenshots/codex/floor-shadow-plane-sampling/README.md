# Floor-shadow edge diagnosis

Baseline `c74b6cf5d`, native Metal Apple M4 Max, 2560×1440, effective subdivision
2. Capture run ended CLEAN. No rendering change ships in this slice.

The isolated GRID box casts onto the SDF receiver plate. All four cardinal views
pass the existing area/IoU check but fail the optional strict projected-edge
check. Its expected polygon comes from authored box corners projected along the
sun direction onto the floor. It uses no reference shadow image or renderer
shadow samples. Floor bounds calibrate framebuffer scale.

| Yaw | Capture | IoU | Area ratio | Missing shadow pixels | Excess shadow pixels |
|---|---|---:|---:|---:|---:|
| 0 | 1502 | .927 | 1.020 | 14 | 98 |
| 90 | 1503 | .967 | 1.023 | 3 | 157 |
| 180 | 1504 | .956 | 1.027 | 39 | 387 |
| 270 | 1505 | .940 | 1.032 | 29 | 499 |

Counts exclude a fixed one-framebuffer-pixel **8-neighbor/Chebyshev** boundary
band, caster-colored pixels and their immediate neighbors. This is a raster
inclusion allowance, not a Euclidean distance threshold, blur or enlarged shadow.
Cyan diagnostic pixels are missing shadow; red are excess. The overlay's red
polygon is the independent projected boundary.

![270-degree polygon overlay](yaw270-oracle.png)
![270-degree edge errors](yaw270-edge-errors.png)

## Reproduce

```sh
fleet-run IRCanvasStress --only shadowbox,floor --probe-grid --pivot-origin --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 2 --auto-screenshot 6 --sweep-yaw 0 4.71238898 4
python3 scripts/render-shadow-box-metric.py --grid --strict-edges docs/pr-screenshots/codex/floor-shadow-plane-sampling/capture-1502.png docs/pr-screenshots/codex/floor-shadow-plane-sampling/capture-1503.png docs/pr-screenshots/codex/floor-shadow-plane-sampling/capture-1504.png docs/pr-screenshots/codex/floor-shadow-plane-sampling/capture-1505.png
```

The strict command intentionally exits 1. Omitting `--strict-edges` preserves the
existing aggregate acceptance thresholds and passes all four. Optional
`--overlay-dir` writes the polygon and error images.

## Controls and limitations

Literal rectangles and diagonal diamonds at two scales pass. A shifted equal-area
shadow, added tooth, missing interior, blank frame, fully hidden expectation,
clipped polygon and an expectation consumed by the boundary allowance fail.
Partial caster occlusion is excluded while remaining floor pixels are checked.
The oracle is specific to this blue box, neutral floor, sun and four cardinal
views; color segmentation does not validate arbitrary materials or penumbrae.

The strict check exposes disagreement with a sharp projected polygon. It does
not attribute that disagreement among finite sun-map resolution, receiver sample
reconstruction, and the current one-lighting-value-per-trixel presentation. It
is not a claim that the current representation can express every edge exactly.
Next work must distinguish those losses and retain geometric coverage through
presentation where needed, without smoothing away legitimate voxel geometry.

## Rejected experiment

Correcting world-aligned finite PCF tap depth by the stored caster-face plane
changed **zero RGB pixels** in each of four matching captures (1506–1509).
The experiment was reverted; it did not address this isolated boundary defect.
An initial Metal launch failed to compile because the experimental block used
GLSL vector spellings; after correcting the spellings the four-shot run exited
CLEAN. Neither that compile error nor experimental shader edits ship.
