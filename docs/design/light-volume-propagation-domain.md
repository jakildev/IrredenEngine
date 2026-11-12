# Exact light-volume propagation domain

Local lighting retains the full 128³ camera-anchored textures and the existing
six-neighbor propagation rule. Only the dispatched region changes. This does
not alter sun-shadow geometry, trixel reconstruction, light radius, iteration
count, neighbor order, occlusion, or RGBA8 storage.

Let `Q` be the texel coordinates written by the seed pass and `K` the existing
propagation count. The fixed half-open box for the entire frame is

```
[min(Q) - K, max(Q) + K + 1) intersect [0, 128)^3
```

Round its lower bound down and upper bound up to the `(8, 8, 4)` threadgroup.
`lightVolumePropagationDispatch` derives `Q` from the uploaded
`GPULightSource::originAndType_`, using the seed shader's integer conversion
and camera-anchor offset. The true light apex can differ after boundary
clamping or occlusion-aware relocation and cannot supply this bound.
`LightVolumeParams::propagationOrigin_` offsets invocation IDs; the existing
volume bounds check still applies. The domain stays fixed across all `K`
iterations, so there are no per-iteration CPU uploads.

## Why omitted texels are exactly zero

Both color ping-pongs start at zero. When spotlight IDs are carried, their
ping-pongs also start at zero. At iteration zero, nonzero payload exists only
at seeded cells. Each iteration retains a cell's own value or selects one of
its six neighbors. It can therefore extend nonzero support by at most one
Manhattan step. Occlusion rejects candidates and cannot extend support.
Induction puts every possible nonzero texel through iteration `K` inside the
box above. Aligned padding adds work but removes no reachable cell.

This argument uses the iteration count, not an estimate of when the light
fades. RGBA8 rounds every iteration; black sources can still carry alpha/IDs,
and sources whose alpha rounds to zero can still carry RGB/IDs. All staged
sources participate in the bound.

Clearing only the initial read texture is insufficient: a cropped dispatch
does not overwrite the destination's exterior, and the next swap would expose
stale values. The nonempty path clears both color/ID pairs. With no sources,
only the current read pair needs clearing because no propagation or swap occurs.
The no-spotlight shaders do not consume or propagate IDs.

## Correctness controls

`LightVolumeDispatchTest` checks alignment, inclusive edge coverage, staged
versus true positions, and containment of reachable Manhattan cells.
`MetalGpuComputeDispatchTest.BoundedLightVolumeMatchesFullVolumeAfterEveryIteration`
executes the real clear, seed and propagation kernels, starting both arms from
the same seeded snapshot. It compares the entire volume after each iteration
and includes poisoned destinations, moving/absent lights, boundary sources,
black/quantized-alpha sources, ID transitions and independently anchored voxel
and SDF blocker fields. Omitting the destination clear is a negative control.

The shader test qualifies native Metal. OpenGL mirrors the dispatch offset
and parameter layout but requires its own native validation. Same-cell seed
stores remain nondeterministic; independent seed dispatches with colliding
sources are not an equality oracle. The existing multi-canvas spotlight
restriction also remains in force.

## Occlusion-grid read domain

The CPU occlusion grid uses a conservative world-cell box that covers both
propagation neighbor reads and boundary-seed relocation. Each eligible local
light contributes a box around its clamped seed: reach `K + 1` for an
unclamped seed and `2K + 1` for a clamped seed. Their union is clipped to the
camera-anchored light-volume window grown by one cell. Pool chunks with a
current world AABB are visited only when that AABB, grown by half a cell for
round-half-up assignment, intersects the box. Every visited voxel is still
clipped to the box, so chunk admission state cannot change the grid.

World-AABB debt is paid without coupling the world and cardinal cull caches.
A stale chunk is admitted without an AABB test. Before admission, a frame
whose stage-1 cull uses world bounds refreshes all stale world AABBs; another
frame refreshes at most 64, oldest first. A cold or resized cache first becomes
per-chunk debt, and each refresh clears only the world-cache pending bit. The
no-light path touches neither cache. This moves refresh work ahead of stage 1
on continuous-yaw and spatial-view frames while bounding added work at a
cardinal pose. A frame's class follows stage 1's branch on that frame, so an
exact-cardinal frame reached during rotation remains budgeted and admits its
unpaid chunks stale.

## Cost model

One fixed union box favors localized lighting. Widely separated sources can
cover the full volume and retain the full propagation cost plus the additional
clear. Measure the complete light-volume stage, including that clear, with
identical window mode and pacing in each control. This changes neither the
volume's memory footprint nor the occlusion grid's fixed clear and upload sizes.
