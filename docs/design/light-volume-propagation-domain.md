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

## Cost model

One fixed union box favors localized lighting. Widely separated sources can
cover the full volume and retain the full propagation cost plus the additional
clear. Measure the complete light-volume stage, including that clear, with
identical window mode and pacing in each control. This changes neither the
volume's memory footprint nor the cost of constructing the occlusion grid.
