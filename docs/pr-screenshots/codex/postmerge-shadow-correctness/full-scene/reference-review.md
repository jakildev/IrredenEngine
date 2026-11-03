# CanvasStress macOS reference A/B review

Candidate: base `0ac961ac2b48dfe2ac3495a01ab2c3fc97111ac7` plus the working-tree continuous per-axis face-selection guard. Per-run manifests record the source state and shader fingerprints. The eight existing `macos-debug` PNG references were compared with fresh captures from the same demo settings: default shots 0–5 and compare shots 12–13. No reference PNG or threshold was changed in this review.

The six full-scene cases differ at 2,616–3,792 exact pixels each (0.071–0.103% of 2560×1440); the two comparison cases differ at 368 and 672 pixels (0.010% and 0.018%). Max single-channel deltas range 107–182. Detailed counts, changed-pixel bounds, and occupancy transitions are in `reference-comparison.json`.

The changed areas follow GRID entities in the orbit ring, central group, and GRID_SPIN comparison cube. Magnified reference/new/difference crops show lost false face-edge and stripe pixels while retaining the cubes' front and top surfaces. The entire object placement and floor remain visually fixed; no broad new region appears. Across all eight images only 24 pixels change from black to occupied; most changes are occupied-face recoloring or obsolete face pixels becoming black. This is consistent with the face-selection guard's intended removal of camera-back-facing risers on continuous GRID routes.

Verdict: refresh exactly these eight macOS references from the fresh candidate captures, without relaxing thresholds. The refresh is a recorded-pose visual check. It does not certify all silhouette boundaries or rotated GRID fog-cut behavior; the focused orbit ray oracle and normal-facing structural gate retain those separate roles.
