# Bounded source-face overflow reference

The receiver has a **default-off diagnostic** that replaces sampled fallback
on an incomplete tile with an exact query of the complete face-record pool,
provided the pool contains no more than 256 faces. It preserves the scene,
sun map, index, tile counters, face geometry, bias and shadow-throw window.
This isolates the query transition implicated by
[the source-face index diagnostics](sun-face-index-diagnostics.md).

The reference is not a production overflow solution. At 257 global records,
an incomplete tile resumes sampled fallback. Adding unrelated distant faces
can therefore revoke exactness at a receiver. The ceiling bounds each rescue
query but does not demonstrate efficient million-entity behavior, nor does it
repair overflow in a large record pool.

## Query contract

`sampleCascadeShadow` retains the ordinary tile list whenever its count is at
most 64, including complete tiles in a globally exhausted pool. For an
incomplete tile, the reference may inspect records `[0, sourceFaceCount)` only
when the count fits both the 256-face query budget and the allocated record
capacity. Every recorded finite face is then available, so an exact miss may
skip the indexed surface fallback layer. The independent non-indexed layer
remains eligible. Out-of-map, non-surface and back-facing receivers do not
run the rescue query.

No extra buffer, index write, readback or larger allocation is introduced.
Each rescue invocation performs at most 256 finite-face intersection tests
and stops on the first accepted blocker. A receiver in a cascade blend can
invoke the sampler twice. The reference uses the existing finite-face
floating-point predicate; “exact” means geometric face coverage rather than
sun-texel coverage, not exact arithmetic or an independently validated screen
receiver reconstruction.

## Enable and compare

In both `ir_sun_shadow_sample.glsl` and its Metal twin, change the default of
`IR_SUN_FACE_OVERFLOW_REFERENCE` from `0` to `1` for the candidate capture.
Leave `kSourceFaceOverflowReferenceBudget` at 256. Rebuild/stage the shaders
before each native arm; restore the switch to zero for the final control.
No CLI flag changes this compile-time experiment.

Use the same scene, binary and camera commands in every arm. The reproduction
in [the diagnostic report](sun-face-index-diagnostics.md#reproduce) supplies
the overhead-light oblique box fixture. Capture 63, 64 and 128 boxes, each
with the same floor: these emit 64, 65 and 129 records. The first should
remain unchanged; the other two exercise the rescue on incomplete tiles.
The demo caps this fixture at 128 boxes. For a native above-budget control,
retain 128 boxes and use oblique sunlight with nonzero components along all
three box axes; confirm with the probe that more than 256 records were
emitted. The precise 256/257 record boundary is covered by the CPU suite.
Keep the probe off for performance measurements. Native image and timing
evidence must be recorded separately from these host-side tests.

## Executable controls

```bash
python3 scripts/tests/test_render_source_face_overflow.py
python3 scripts/tests/test_render_mixed_source_shadow.py
python3 scripts/tests/test_render_source_face_query.py
```

The overflow suite executes the production receiver through its exact face
query and finite-surface fallback branches using C++ adapters for both shader
backends. It tests 64/65, 256/257 and global-capacity boundaries, the final
record as the only blocker, complete-list record IDs, complete tiles in an
exhausted pool, strict bias/throw boundaries, closed edges and preservation of
the other surface layer. Instrumented buffer reads check the rescue ceiling
and unchanged ordinary-list work. The default-off contract is asserted.

Each enabled backend agrees with an independent double-precision
triangle-ray oracle for 61,934 rays through rotated, mirrored, edge-swapped
and sloped overlapping faces: 4,519 hits and 57,415 misses. Seven mutations
per backend reject disabled rescue, truncated scans, a removed budget guard,
wrong records, sampled fallback after an exact miss, lost other-layer shadows
and ignored tile-list IDs. These tests do not certify GPU ordering, index
construction, legacy PCF, cascade selection or displayed receiver geometry.

## Rendering modes remain distinct

Caster representation and receiver routing are separate decisions. Having
finite caster records does not imply that a displayed receiver queries them.
The GLSL symbols below have corresponding Metal implementations.

| Path | Current producer/receiver contract | Reference coverage |
|---|---|---|
| Rigid detached `SOURCE_FACES` with world receive | `bakeVoxelFaces` selects rigid basis 2; `c_bake_voxel_sun_faces` transforms original finite cells. `f_source_face_scatter` calls `worldSurfaceSunShadowFactor` at interpolated face positions when continuous lighting is enabled. | Eligible incomplete tiles in a complete small pool. |
| Revoxelized detached `LOCAL_TRIANGLES` with world receive | The voxel-face caster reads the uploaded resampled occupancy. `c_lighting_to_trixel_body` uses the displayed trixel centroid and calls `worldSurfaceSunShadowFactor` when `visibleFaceIds.w == 2`. | Eligible query overflow only; preserve actual occupancy steps and trixel sampling. |
| Attached GRID/per-axis voxels | Voxel cells can cast finite indexed faces, but `c_compute_sun_shadow_body` and `c_light_overflow_faces` call `worldSunShadowFactor`, which uses an offset raster receiver and bypasses finite querying. | No rescue; receiver routing remains sampled. |
| Eligible analytical BOX receiver | `ir_trixel_to_framebuffer_body` calls `selectedShapeBoxReceiver`, then the finite query directly for shadow overlay or through `shapeSurfaceLighting` for normal lighting. `shapeBoxReceiver` rejects hollow, entity-rotated and unsupported lattice/mode combinations; procedural colors preserve their fallback lighting. | Only eligible continuous receivers, not every BOX display mode. |
| Non-box SDF casters | `system_shapes_to_trixel` submits non-box shapes through `shapeCasterProgram_`, `prepareAnalyticCasterDepth` and `bakeAnalyticCasterDepth`; `c_bake_sun_shadow_map` projects sampled canvas depths. | No new finite caster geometry; these remain in the other depth layer. |
| Legacy-depth or particle-enabled frame | `beginVoxelFaceCoverage` disables finite coverage when either particle render system is registered or `voxelFaceCoverage_` is false. `BAKE_SUN_SHADOW_MAP::tick` uses the canvas-depth bake. | No finite pool to rescue. |

`c_bake_box_sun_shadow` indexes the sun-facing faces of visible BOX
descriptors, independently of the more restrictive continuous-receiver
eligibility above. Detached canvases without world receive retain their
existing lighting; changing an index query cannot opt them in.

The remaining-mode followup should first compare caster geometry and receiver
world positions against what each mode displays. A continuous displayed face
needs a clean projected boundary. Revoxelized occupancy must cast its actual
steps; replacing that geometry with an authored smooth box is also incorrect.
For attached GRID/per-axis faces, a finite receiver route needs a demonstrated
surface-position contract, including sub-cell phase and overflow faces, before
replacing the raster-origin query. For currently sampled SDF modes, establish
whether the display is continuous or lattice-based, then provide corresponding
finite caster and receiver geometry. The small-pool reference neither supplies
missing geometry nor justifies blur, footprint dilation or bias adjustment.

## Native same-geometry result

Apple M4 Max, macOS 26.5.2, Metal Debug, 2560×1440. The three arms use the
same demo binary, scene commands and camera angles; only the sampler switch
changes. Baseline engine commit was `2183e854875dfa003f35fc03d30f198d1cf624bb`;
binary SHA-256 was
`bf2037d48dde174b6e8d80fb154e97b58fab9a64a1a445cc34958d5e32d016ff`.
The final arm restores the switch to zero. All 16 restored images are
RGB-identical to baseline, and all 16 restored tile tables are byte-identical
after decompression to the corresponding enabled-reference tables.

| Fixture | Requested records | Changed RGB pixels at yaw 0° / 90° / 180° / 270° |
|---|---:|---|
| 63 boxes, overhead sun | 64 | 0 / 0 / 0 / 0 |
| 64 boxes, overhead sun | 65 | 476 / 528 / 540 / 536 |
| 128 boxes, overhead sun | 129 | 532 / 632 / 696 / 624 |
| 128 boxes, oblique sun | 387 | 0 / 0 / 0 / 0 |

The reference removes the fine sampled edge teeth in the overflowing,
under-budget views without changing geometry or index completeness. The
below-threshold and above-budget controls remain unchanged. This is causal
evidence for the query transition in this fixture, not a native per-pixel
oracle for every displayed surface or shadow mode. The host-side independent
ray oracle supplies separate coverage of the finite-face predicate and query
policy. Native OpenGL rendering remains unmeasured.

[Screenshots](../pr-screenshots/codex/bounded-exact-shadow-overflow/README.md),
[pixel comparisons](bounded-source-face-reference/comparison.json),
[restoration checks](bounded-source-face-reference/restoration-comparison.json)
and [probe summaries](bounded-source-face-reference/probe-summary.json) are
retained alongside the raw manifests, reports and compressed tables. The
first three baseline cases' CSV files were overwritten by subsequent demo
captures; their images and manifests remain. The restored arm preserves
all four cases' tables and verifies the baseline images.

Each capture uses the common arguments below, with count 63, 64 or 128. For
the above-budget control use count 128 and sun `-0.42 -0.60 -0.55` instead.
Archive the generated CSVs before starting another case.

```bash
python3 scripts/perf/repeat_profile.py --target IRCanvasStress --output /tmp/shadow-reference-count128 --repeats 1 -- --only shadowbox,floor --probe-analytic-box --no-spin --no-auto-rotate --pivot-origin --zoom 3 --subdivisions 1 --auto-profile --auto-screenshot 6 --sweep-yaw 0 4.71238898 4 --analytic-box-count 128 --analytic-box-yaw 0.785398163 --analytic-box-step 0.03125 0 0 --analytic-box-yaw-step 0.001 --sun-direction 0 0 -1 --sun-face-index-probe
```

## Separate timing control

Two forward/reverse timing rounds per arm used `box_shadow_controls.py
--suite oblique --rounds 2`, without the synchronized index probe. Raw reports,
commands and binary manifests are under
[`performance/`](bounded-source-face-reference/performance/). These are small
Debug scenes on battery on a shared host, with unrelated builds possible.
They do not qualify large-scene throughput or establish a speedup.

| Fixture | Baseline mean frame ms | Reference mean frame ms |
|---|---:|---:|
| 1 box, yaw 0° | 9.480 | 9.405 |
| 1 box, yaw 45° | 9.505 | 9.420 |
| 64 boxes, yaw 45° | 9.415 | 9.415 |
| 128 boxes, yaw 45° | 9.490 | 9.460 |

The zoom-3 synchronized visual captures above are excluded from timing
conclusions. Sampled GPU stages in the timing reports are noisy and cannot
be added into an end-to-end frame budget. A production design needs larger
overlap and unrelated-caster controls before any performance claim.

## Next scalable experiment

The current allocation spends `32768 * 64` record-index slots on fixed tile
lists, regardless of occupancy. Replacing those fixed lists with pooled
contiguous tile ranges can redistribute the same total index budget toward
hot tiles. A GPU count/prefix/fill sequence would retain one count and start
offset per tile and index the existing face records, with explicit incomplete
state when the total index pool is exhausted. This is preferable to simply
raising every tile's capacity, but introduces dispatch, prefix-scan and
publication costs that need native measurements. A linked overflow-page pool
uses fewer passes but adds allocation contention and scattered receiver reads.

Neither layout alone bounds the exact receiver cost independently of overlap.
A genuinely hot tile can contain thousands of finite faces; an exact miss
must rule out every relevant blocker. The next query experiment should use a
spatial/depth hierarchy over complete recorded faces or populated tile ranges,
with conservative sun-UV bounds and sun-Z intervals to prune a receiver's
finite ray segment. Leaf queries can reuse `sourceFaceRaySeparation`. A GPU
build from the current frame's records respects existing ownership and avoids
CPU dirty-state tracking. Tree depth, traversal storage, actual query work and
global record exhaustion remain explicit design constraints; a fixed traversal
budget cannot label an unfinished query as an exact miss.

Before choosing a layout, capture true-overlap pressure, irrelevant AABB
candidates and the global-record count separately. Compare dense coincident
casters, separated oblique casters with overlapping AABBs, empty-ray queries,
and the same local scene with many unrelated remote casters. The latter must
not change local exactness merely by crossing a global small-scene threshold.
The deferred [tile rejection experiment](projected-face-tile-overlap.md)
addresses false candidates only and cannot solve genuine overlap. Any
large-scene claim must also account for the existing 65,536-face global cap.
