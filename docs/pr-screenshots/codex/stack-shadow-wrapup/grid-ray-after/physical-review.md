# GRID shadow reference review

The new shadow-overlay path was evaluated against the prior integrated
renderer using the retained native Metal captures. All 27 default/compare
beauty screenshots are RGB-identical before and after the overlay-only change;
both upright/rotated normals overlays are also RGB-identical. The corrected
shadow overlays contain only black (lit) and magenta (shadow) pixels, including
overflow faces. The current run's sun-face index has 1,728 upright and 2,214
rotated finite-face records; every nonempty tile is complete (max 21/36
records against capacity 64), so this sparse fixture does not depend on an
incomplete-tile fallback.

The independent `gridspin-selfshadow-oracle.py` mirrors the documented
destination-lattice inverse resampling of a solid 12³ GRID cube and casts
sun rays from exposed voxel-face centers against finite occupied cells. It
also computes the equivalent projected finite-quad separation used by the
source-face shader. Across 0°, 15°, 30°, 45°, 60°, 75°, and 90°, the box-ray
and projected-quad classifications have zero disagreements. The sun-facing
upright cube has 0/144 self-occluded centers on each of -X, -Y, and -Z.
At 30° about Z, 60/192 -X and 66/192 -Y centers are genuinely blocked by
the digital staircase; top -Z remains 0/144. A conservative interior ROI on
the **rightmost red Z-spinning cube** (x=1900..2249, y=150..629) has no
magenta in the upright capture. After the overlay fixes overflow coloring,
the rotated capture has 228 magenta pixels on -X-normal facets and 5,284 on
-Y-normal facets, with none on top-normal facets. This agrees with
the oracle's qualitative face pattern.

The master-to-integrated no-shadow controls remain RGB-identical (12/12
default and 15/15 comparison views), while shadow-enabled captures change.
The change is consistent with intentional finite voxel self-occlusion and
is a defensible **scoped reference refresh**. This analysis does not prove
every changed pixel: display projection, sampled positions, finite-index
query routing, quantization, other scene casters, and approximate fallback
outside the sparse fixture are separate limits. Existing orbit banding is
not accepted as correct by this result.

Machine detail: `physical-review.json`; retained native captures and CSVs
are in the sibling case directories. The standalone oracle writes
`/tmp/codex-gridspin-selfshadow-oracle-results.json` when run.
