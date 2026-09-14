# Rendering audit worklist

Keep the agreed work in this order. A diagnostic experiment is not an implemented
fix, and a small native scene does not establish fleet-scale rendering throughput.

## Agreed work

| Priority | Work | State / next acceptance |
|---|---|---|
| 1 | Detached face geometry and trixel display | Investigated in [display diagnosis](detached-trixel-display.md). Visible source faces, depth and picking still need a consistent projection. Origin Y compensation corrected; this does not fix dilation or face striping. |
| 2 | Shadow reception and contact | Source-face casting exists, but receivers still use reconstructed surfaces. Check concave faces and contact after visible geometry agrees. |
| 3 | Shadow edges and filtering | Source casting improves outlines. Reassess residual jaggedness without masking geometry errors with blur or bias. |
| 4 | Duplicate CPU occupancy reconstruction | Profile identified work overwritten by inverse GPU resampling. Preserve buffer-availability fallback and identity transitions before skipping it. |
| 5 | Mode and scale validation | Extend density, screen-lock, pan, cascade, sparse/elongated asset and OpenGL coverage. Measure representative large populations and GPU cost before changing defaults. |

Attached probes and corrected authored probe placement are already in the stack.
The source-face shadow paths remain opt-in. Ready-for-review PRs do not imply
that the experimental rendering is ready to become the default.

## Proposed follow-ups discovered during this work

| Priority | Finding | Evidence / next check |
|---|---|---|
| High, within geometry/contact | Source-shadow half-cell convention may disagree with visible geometry | `kVoxelRasterCellAnchor` subtracts half a cell for visible mass; the source shadow kernel currently builds corners from the source position through position+1. Reconcile the world-space convention and correct the shadow oracle independently; do not treat its existing pass as proof of alignment. |
| High, within geometry | Detached centroid changes with camera yaw | After removing the constant Y offset, the visible box placement check still fails at 22.5, 67.5 and 90 degrees. GRID controls pass across all five views. Separate lattice anchoring and dilation asymmetry from composite placement. |
| Medium, within geometry | Detached dilation expands the visible box | At yaw zero, zoom 2, its area is 1.219× the analytical box. The origin fix leaves that ratio unchanged. Preserve concavities when replacing dilation. |
| Medium, validation | Small-voxel and picking coverage | Add a magnified isolated voxel with triangle-ID/face-color controls and a picked-surface oracle. A box silhouette cannot establish correct internal face shading or picking. |

## Origin correction evidence

The detached quad places the model origin by compensating for the canvas's
`(-1,-1)` texel origin offset. Texture Y increases down; framebuffer Y increases
up. Applying `(+texelX,-texelY)` instead of `(+texelX,+texelY)` removes the
two-texel vertical displacement without changing the depth key or caster geometry.
No buffer allocation, upload or draw is added.

The visible-box oracle projects the centered authored mass, with corners at
`±(halfCenterSpan + 0.5)`. It calibrates screen scale from the complete receiver
plate and uses no renderer depth samples. The shared projection hull/plate helpers
are also used by the existing shadow oracle; its thresholds and projection
conventions are unchanged in this slice.

At 2560×1440, yaw zero, zoom 2:

| Capture | Centroid error (pixels) | Visible area / expected | Placement |
|---|---:|---:|---|
| Original detached | (+0.5, -8.0) | 1.219 | Fail |
| Corrected detached | (+0.5, 0.0) | 1.219 | Pass |
| Attached GRID control | (+0.5, -1.0) | 1.002 | Pass |

`--placement-only` accepts at most two pixels of error per axis, allowing the
plate calibration/raster quantization. The default additionally requires silhouette
IoU ≥.95 and area ratio .95–1.05. All five GRID captures pass the default check;
the corrected detached silhouette still fails. These outcomes intentionally keep
the remaining geometry defect visible.

```sh
fleet-run --timeout 120 IRCanvasStress --only shadowbox,floor --no-spin --no-auto-rotate --no-ao --subdivisions 1 --zoom 2 --auto-screenshot 6 --sweep-yaw 0 1.57079633 5 --source-face-shadows
python3 scripts/render-visible-box-metric.py yaw0.png --placement-only
python3 scripts/render-visible-box-metric.py yaw45.png --yaw 45
```

Add `--probe-grid` to capture the attached control. Keep the complete plate in
view; the oracle is specific to this scene and does not validate arbitrary assets,
lighting, internal face boundaries or temporal stability.

Native Metal sweeps exited cleanly. Existing source-shadow checks remain 4/4 after
the placement change (yaw-zero IoU .712, the least margin). OpenGL runtime remains
unverified; the corrected C++ placement is shared by both backends.

Retained evidence: `docs/pr-screenshots/codex/visible-box-oracle/`. Capture 135
uses the original placement from baseline `a2c55707add8ba1a74d55091f17f203c92160084`;
125–129 use corrected detached placement at 0/22.5/45/67.5/90 degrees; 130–134
are the GRID controls at the same angles. Captures 136–140 are the corrected
four-probe scene. For example:

```sh
python3 scripts/render-visible-box-metric.py docs/pr-screenshots/codex/visible-box-oracle/capture-125.png --placement-only
python3 scripts/render-visible-box-metric.py docs/pr-screenshots/codex/visible-box-oracle/capture-132.png --yaw 45
```
