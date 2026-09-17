# World-scale visibility and simulation budgets

The performance target is one million simple resident entities, representative
lighting, continuous camera yaw and zoom, and sustained 60 fps at a fixed output
resolution. It is a target, not a demonstrated capability. Record hardware,
backend, build type, live entity/voxel counts, useful visible work, and frame-time
distribution. A million separate complex canvases is a different workload.

## Visibility contract

The default chunk viewport query has no near/far distance cutoff. Its region is
the preimage of the 2D viewport under the live orthographic projection: an
unbounded prism along the viewing direction. Under yaw, project a chunk's world
AABB using the same yaw and camera-pivot convention as rendering, then test its
2D bounds against the viewport. AABB overlap is conservative: it can admit
extra chunks, but must not reject geometry that contributes visible coverage.

`IRMath::isoAABBOfWorldAABBUnderYaw` already bounds the eight projected corners.
`C_VoxelPool::rebuildChunkBounds` uses cached world bounds during continuous yaw;
`IRSystem::buildChunkVisibilityMask` tests their 2D overlap. Static geometry
requires projection per chunk, not a new scan of every voxel for each yaw.
Raster cell anchoring and effective camera offsets remain part of the query.

Unbounded visibility is not infinite numerical precision or unlimited depth
storage. Large-coordinate transforms, depth encoding, streaming residency and
lighting domains have their own finite limits. Audit those independently;
do not hide a precision or residency limit behind an implicit draw-distance
cutoff. Off-screen shadow casters use a conservative light-directed region,
separate from visible coverage and bounded by the shadow representation's
documented reach. Finite light reach is not a camera far plane.

The regression tests `ContinuousYawViewportHasNoDepthCutoff` and
`StaticChunkReentersViewportAcrossFullYawTurn` exercise the actual CPU mask:
deep positions on both sides of the origin remain visible along the viewing
ray, lateral positions are rejected, and a static chunk enters and leaves the
view during a full turn without rebuilding its world bounds each frame.
These are viewport-mask tests, not proof of unlimited GPU depth range or a
replacement for end-to-end screenshot comparisons.

## Spatial organization

Renderer pool chunks are consecutive allocation-slot groups, not world-space
streaming chunks. Unrelated distant objects sharing a group produce a broad
AABB and poor rejection even when the projection is correct. Measure that
bound inflation before adding another projection algorithm or enabling more
aggressive occlusion rejection.

A spatial hierarchy should index existing occupied regions and query their
conservative bounds. Do not enumerate an infinite set of world chunks along
the viewing ray. Broad, shallow worlds are favorable, but tall objects and
displaced geometry must remain covered. Preserve allocation identity and
attachment semantics if spatial membership and GPU storage differ.

## Simulation and presentation

Render visibility, shadow relevance, simulation interest and residency are
separate decisions. An invisible entity can still affect simulation; a visible
entity can render between reduced-rate updates. The engine already exposes
per-system cadence, offsets and accumulated fixed-step delta. A renderer mask
must not silently become the simulation scheduler.

Reduced cadence needs suitable integration: elapsed-time scaling alone does
not preserve collision, encounter ordering or nonlinear dynamics. Benchmark
staggered updates, bounded catch-up, and presentation interpolation separately.
Interest transitions need hysteresis and a bounded promotion workload so camera
movement does not create a simulation spike. Domain-specific policy stays with
the application; the engine supplies scheduling and spatial-query primitives.

## Work sequence

1. Establish honest 64³, intermediate, and 100³ entity controls with verified
   pool capacity and actual spawned counts. Separate frozen, rigidly moving and
   independently moving populations; compare cardinal and continuous yaw at
   matched screen coverage. Report startup, steady frame time and tail latency.
2. Count visited/admitted chunks, retained voxels, expanded subdivision samples,
   occupied face cells, overflow entries, scratch bytes and shadow-only work.
   Measure scattered versus spatially grouped allocation at identical geometry.
3. Improve hierarchical viewport rejection before per-voxel compaction and
   subdivision. Preserve unbounded depth, moving bounds, camera pivots,
   negative coordinates and off-screen caster coverage. Keep rotated Hi-Z
   disabled until its depth/coverage proof is established.
4. Reduce repeated face storage/finalization and capacity-sized dispatch work
   using measured live populations. Retain reconstruction and temporal gates.
5. Bound local-light propagation and AO work by affected regions without
   removing geometrically relevant contributors. Measure quality options only
   after identifying the dominant costs.
6. Measure scheduler, transform propagation, attachments and uploads under
   reduced/staggered cadence, including fast camera pans and moving entities
   crossing spatial groups. Test simulation progress independently of rendering.
7. Repeat representative controls in Release, with profiling disabled, and on
   OpenGL. Audit large-world precision/depth separately from visibility policy.

The [rotation/subdivision audit](rotation-subdivision-audit.md) remains the
canonical optimization checklist and links completed slices.
