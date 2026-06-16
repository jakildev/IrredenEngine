# canvas_stress structural baselines — T-1 (#1767)

Premise-settling first step of epic **#1766** (deterministic render-validation
framework). Establishes committed structural-metric baselines on **current
master** (`8fb2b934`, macos-debug / Metal) and answers, with data, whether two
live render symptoms still reproduce. Reported to **#1717**.

Structural metrics (occupancy classification + connected-component counts) are
robust to the sub-pixel CPU↔GPU float jitter that excludes the zoom shots from
the pixel-diff gate, and are backend-agnostic — so the thresholds below apply to
`linux-debug` too. The PNGs under `macos-debug/` are the Metal capture they were
derived from; the numbers live in [`metrics.json`](metrics.json).

## How to reproduce

Two new shots were added to `canvas_stress` (`g_allShots`, appended after the
manifest suite so `render-verify` ignores them — these feed the structural
metrics, not the pixel-diff gate, which T-2/#1768 wires). A run-global
`--debug-overlay <mode>` flag forces the requested debug overlay for the whole
capture.

```
# (a) cast-shadow occupancy — magenta=shadowed, black=lit:
fleet-run IRCanvasStress --auto-screenshot 120 --debug-overlay shadow
#   shot "shadow_overlay_floor" = screenshot_000009.png
python3 scripts/render-shadow-metric.py <shot> --roi 1010,540,450,250

# (b) re-voxelize surface coverage (normal render):
fleet-run IRCanvasStress --auto-screenshot 120                 # shot "revox_coverage"
fleet-run IRCanvasStress --auto-screenshot 120 --solo-revox    # lone L-prism, clean vs black
```

## Verdicts

### (a) Swiss-cheese cast shadows — **REPRODUCES** (post-#1734)

On the SHADOW debug overlay, the cast shadow that the GRID spin cubes + the
grounded re-voxelize cast-proof cube (#1596) drop onto the #1587 SDF floor is
**not a coherent shadow** — it shatters into pixel-level dithered speckle.

| metric (ROI 1010,540,450,250) | value | clean expectation |
|---|---|---|
| `hole_ratio` (lit holes inside the cast-shadow footprint) | **0.873** | < ~0.3 |
| `components` (4-connected shadow blobs) | **62** | a handful (< ~8) |
| `largest_frac` | **0.525** | ~1.0 |

Full-frame: 101 components, largest_frac 0.336. See
`macos-debug/shadow_cast_roi_crop.png` — the magenta cast shadow is a cloud of
disconnected specks around the (purple) cast-proof cube, the swiss-cheese /
dithering failure mode of **#1717 items 3-4**. Captured at cardinal camera yaw
with the casters self-spinning; the under-camera-rotation case (#1724/#1734)
should also be gated by the follow-up — see the fix ticket.

### (b) Re-voxelize coverage holes — **DOES NOT REPRODUCE**

| solid (ROI) | `fill_ratio` | `interior_hole_ratio` |
|---|---|---|
| grounded dense cube (clean control) | **1.000** | 0.000 |
| solo carved/multi-color L-prism | **0.991** | 0.009 |

The detached re-voxelize solids are coverage-complete. The residual 0.9% on the
carved L-prism is the **documented fine round-to-cell speckle** (sub-cell
CPU↔GPU divergence, expected per P1/P3 — see the canvas_stress manifest notes),
not the #1619-class missing-**face** defect. The #1619 GRID-half fix (#1732)
holds (GRID cubes render clean). See `macos-debug/solo_lprism_crop.png` and
`revox_coverage_scene.png`.

## Magenta-entity anomaly — **EXPLAINED**

The lone magenta/purple entity seen in a normal-colour capture is the **#1596
(P4b-3) grounded world-placed cast-proof re-voxelize cube**, intentionally
tinted `Color{210,120,255}` so its cast shadow is identifiable beside the GRID
cubes'. It is **not** the SHADOW debug overlay magenta (`vec3(1,0,1)` =
255,0,255), which only appears under `--debug-overlay shadow`. The issue's
premise — "the only in-tree magenta source is the SHADOW debug overlay" — is
therefore incomplete: this deliberate entity tint is a second source.

## Follow-ups

- **Swiss-cheese cast shadow** reproduces → fresh fix ticket filed with these
  metric values as its Definition of Done.
- T-2 (#1768) wires `render-shadow-metric` (and the coverage metric) into the
  `render-verify` gate against these baselines; T-3 (#1769) builds the reusable,
  unit-tested coverage / silhouette / clip metric suite (this doc's coverage
  numbers were measured with a throwaway interior-hole computation, documented
  in `metrics.json`).
