# Whole-canvas early depth for source faces

**Status: bounded native correctness controls pass; timing remains provisional.**
The eleven overflow images are unchanged. Eight touching/intersection pairs match
the forced-fallback path in full RGB and sampled printed depth. The broader native
suite retains the same five pre-existing failures (41/46 checks pass).

The [retained parent/optimized/parent matrix](timing/README.md) completed with a
GUARDED quiet report on battery power. Crowded cardinal frame means are 70.60 ms
versus 66.61/67.23 ms for the parent; crowded oblique means are 71.96 versus
68.52/68.89 ms. The parent has incomplete overflow shadows. The extra cost remains
measurable, but the large amplification observed in the initial integration is
absent in this bounded control. All measurements are provisional; no qualified
speedup, Release or million-entity claim follows.

## Rendering contract

The crowded fixture exposed repeated shadow queries from hidden source-face
fragments. Source-face rendering previously supplied framebuffer depth from the
fragment shader. Moving eligible canvases to vertex depth lets the existing
strict `LESS` depth test reject hidden fragments before their lighting queries.
The finite shadow hierarchy and its exact intersection predicates remain in use.

The geometry producer reduces metadata from the faces it actually emits,
including GPU-authored positions. Three formerly reserved words in the existing
32-byte `SourceVoxelFaceHeader` hold maximum absolute model-position magnitude,
maximum decoded face priority, and an invalid-position flag. The first five
indexed-indirect command words and the face-record offset remain unchanged.
Positive finite float encodings support integer atomic maximum for the position
bound. Nonfinite positions set the invalid flag. The existing full-header reset
clears these values each producer frame; existing publication barriers precede
their draw-stage reads. There is no CPU position mirror or new readback required
for classification.

The shared shader predicate accepts a canvas only when all emitted faces have
ordinary priority, all relevant inputs are finite, both depth scales are
positive, and a conservative enclosure stays inside the unclamped world-depth
interval and the framebuffer clip range. The enclosure accounts for the actual
quaternion components, density, depth scale and world-depth offset; it does not
assume a normalized quaternion. A full-cell radius and enlarged analytic bound
provide routing slack. This slack changes eligibility, not rendered depth.
Priority, invalid, overflowing and boundary-crossing cases retain the original
fragment-depth calculation.

Each canvas submits two specialized draws over the same sorted source-face
order and indirect count. The shared predicate clips the entire canvas from
exactly one draw. Whole-canvas selection preserves face ordering across the
early/fallback boundary. The early fragment variant has no fragment-depth
output; the fallback retains the existing clamp and normalization expressions.
Both variants share their geometry, world-position reconstruction, lighting,
shadow queries and fog implementation through shader include bodies.

The implementation lives in the
[source-depth helpers](../../../../engine/render/src/shaders/ir_source_face_depth.glsl),
their [Metal twin](../../../../engine/render/src/shaders/metal/ir_source_face_depth.metal),
and the [entity-canvas composite](../../../../engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp).

## Retained validation

The [visual manifest](visual/manifest.json) records eleven CLEAN native Metal
captures, covering complete and overflowed tiles, overlapping casters,
vertical/oblique sun directions and probe/no-probe pairs. Every captured PNG is
byte-identical to its referenced original hierarchy frame; complete RGBA pixel arrays were compared
independently as well. The archive references those existing PNGs and retains
capture commands, configs, compressed logs, geometry probes and
[pixel comparisons](visual/pixel-comparisons.json).

Those captures identify base `d832ed2ec5de4813e77a309bed0e1ab43719a346` plus a dirty
source overlay and binary SHA-256
`3612cb4cb80e020399045d390de0725c52fd82ed080c063832cc4fa14e25e17d`.
Their original [source patch](visual/source.patch) recorded tracked changes only.
The [additions supplement](visual/source-additions.patch) reconstructs the eight
omitted new shader files from bytes the implementer and capture owner confirmed
were unchanged since creation. It was assembled afterward, not recorded with
the capture. Both patches were verified to apply in order to the recorded base.
The separately frozen performance artifact follows the include-placement cleanup
and has its own source manifest and fingerprints; these visual captures must not
be relabeled as that later build.

The [broader native verdict](visual/native-verdict.json) records 41 passing checks
out of 46. The five failures and their comparator values match the earlier
overflow run: two mode-switch controls, one detach control and two impulse
controls. Prior actual-parent captures establish those failures on the parent as
well. No reference images were refreshed. This JSON is a transcription of the
observed terminal verdict, not a retained full raw log, and establishes unchanged
aggregate comparator results rather than a blanket visual pass.

The [portable early-depth tests](../../../../scripts/tests/test_render_source_face_early_depth.py)
execute both shader dialects' actual routing helpers, producer metadata updates,
vertex selection and depth assignment through host adapters. They check the
32-byte layout and reset, whole-canvas mutual exclusion, priority and invalid
inputs, scale and quaternion bounds, and clamp/clip boundaries. Each backend
admits 817 cases and retains fallback for 1,703 cases; 156,864 independently
transformed corner depths verify the admitted enclosure. Fifteen runtime
mutations per backend are detected. Wrapper preprocessing also confirms that the
early fragment has no depth output and both variants retain lighting and shadow
calls. Serial atomic adapters test aggregation, not native GPU publication.

The existing source-face lighting suite, GLSL reserved-word check and changed
Python formatting checks passed. The full portable render harness initially
passed 112 of 113 suites; its failure identified missing Pillow dependency
registration for the newly added tests. After registration was repaired, all
eleven dependency-contract tests and ten receiver-polygon tests passed. This
records the targeted repair verification, not an unperformed full-sweep rerun.

The [native depth controls](depth-controls/README.md) compare the separately
frozen guarded artifact against an otherwise identical runtime shader overlay
that forces fragment depth. Eight paired Metal cases cover a source box touching
and intersecting a GRID floor, plus source/source upper and side intersections,
at camera yaw `0` and `0.6`. All 16 captures are CLEAN; every pair has zero changed
screenshot RGB pixels and equal final printed depth at framebuffer texel
`(640,360)`. The archive retains the source overlays, original and modified
helpers, artifact hashes, driver, configs, logs, samples and deduplicated images.
The initial background probe at `(321,361)` was rejected as a setup failure and
is excluded from the passing count. These samples do not establish full-buffer
bitwise depth equality or identify the winning source entity.

## Remaining gates and limitations

Vertex-normalized depth interpolation can round differently from interpolating
raw depth and normalizing in the fragment. The eligibility enclosure proves
separation from clamp and clip boundaries; it does not establish identical
framebuffer-depth rounding at coplanar intersections. The eight paired Metal
controls cover the specified intersections at two camera yaws; native coverage
of foreground priorities, clamp transitions and other subdivision settings
remains pending. The single-pixel probe's printed precision cannot resolve every
depth-buffer rounding difference.

The extra draw and vertex-stage eligibility work also run for fallback canvases.
Their cost needs measurement alongside the crowded witness and complete cases.
Native Windows/OpenGL validation remains pending; portable GLSL execution does
not replace driver compilation and depth-buffer comparison.

Timing is provisional while power qualification is unstable. A user-authorized
provisional comparison must still retain its power and quiet-run evidence and
state failed qualifications explicitly. Neither the eleven unchanged images nor
the earlier fixture-only timing experiment establishes a qualified production
speedup, Release throughput or a million-entity target.
