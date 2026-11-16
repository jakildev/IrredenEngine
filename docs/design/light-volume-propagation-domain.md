# Exact light-volume propagation domain

Local lighting retains the full 128³ camera-anchored textures and the existing
six-neighbor propagation rule. This does not alter sun-shadow geometry,
trixel reconstruction, neighbor order, occlusion, or RGBA8 storage.

Let `Q` be the texel coordinates written by the seed passes and `K` the
dispatch reach. `K` is the class radius in a single-class frame and the widest
class radius plus one in a multi-class frame. The fixed half-open box for every
pass in the frame is

```
[min(Q) - K, max(Q) + K + 1) intersect [0, 128)^3
```

Round its lower bound down and upper bound up to the `(8, 8, 4)` threadgroup.
`lightVolumePropagationDispatch` derives `Q` from the uploaded
`GPULightSource::originAndType_`, using the seed shader's integer conversion
and camera-anchor offset. The true light apex can differ after boundary
clamping or occlusion-aware relocation and cannot supply this bound.
`LightVolumeParams::propagationOrigin_` offsets invocation IDs; the existing
volume bounds check still applies. The domain stays fixed across every class's
iterations.

## Why omitted texels are exactly zero

Both color ping-pongs start at zero. When spotlight IDs are carried, their
ping-pongs also start at zero. At iteration zero, nonzero payload exists only
at seeded cells. Each iteration retains a cell's own value or selects one of
its six neighbors. It can therefore extend nonzero support by at most one
Manhattan step. Occlusion rejects candidates and cannot extend support.
Induction puts every possible nonzero texel through iteration `K` inside the
box above. Aligned padding adds work but removes no reachable cell.

This argument uses the widest class plus the residual-shell cell as the
multi-class dispatch reach, not an estimate of when an individual light fades.
RGBA8 rounds every iteration;
black sources can still carry alpha/auxiliary data, and sources whose alpha
rounds to zero can still carry RGB/auxiliary data. All staged sources
participate in the bound.

Clearing only the initial read texture is insufficient: a cropped dispatch
does not overwrite the destination's exterior, and the next swap would expose
stale values. The nonempty path clears both color/ID pairs. With no sources,
only the current read pair needs clearing because no propagation or swap occurs.
Frames without spotlight IDs do not consume or propagate the auxiliary volume.

## Per-light falloff

Each local light uploads an effective radius clamped to the 32-step global
budget. A zero radius retains the historical 32-cell behavior. Boundary seed
discount and occlusion-aware relocation use the radius class assigned to the
light.

A frame runs one shared-falloff pass per radius class, widest first. Each pass
seeds only its class and runs `r` iterations with `stepFalloff = 1 / r` in the
existing color and optional spotlight-ID ping-pongs. A later class writes its
seed only when its alpha is strictly greater than the cell's current alpha.
Propagation then takes the maximum field without discarding an earlier,
shallower wavefront. The auxiliary green channel stays unused.

Every multi-class pass uses the frame-wide box grown by `r_max + 1`. The extra
cell contains the quantized residual shell: after `r` subtractions, RGBA8 may
retain a nonzero last-shell value for radii 27 and 30. A later class can carry
that value one cell farther. The multi-class invariant is therefore:

- no cell is darker than the brightest field produced by any class alone;
- a cell can be brighter only on the first shell past a wider class's last
  shell, by that shell's rounding residual minus the narrower class's step;
- the model's maximum is 5/255, while native validation permits 6/255.

At most 128 propagation iterations run per canvas per frame. If the distinct
class radii exceed that sum, the adjacent pair with the smallest radius
difference merges into the wider class; ties select the widest pair. The merge
map is computed from every eligible light, so `{32, 31, 30, 29, 28}` becomes
`{32, 30, 29, 28}` and costs 119 iterations. A merged light's seed discount
and relocation reach use the wider class radius.

A single-class frame issues the original sequence exactly: one params upload,
one plain seed dispatch over all staged lights, `r` propagation dispatches and
the original `r`-grown box. Single-light and shared-radius scenes are therefore
byte-identical to the prior path.

## Correctness controls

`LightVolumeDispatchTest` checks alignment, inclusive edge coverage, staged
versus true positions, and containment of reachable Manhattan cells.
`MetalGpuComputeDispatchTest.BoundedLightVolumeMatchesFullVolumeAfterEveryIteration`
executes the real clear, seed and propagation kernels, starting both arms from
the same seeded snapshot. It compares the entire volume after each iteration
and includes poisoned destinations, moving/absent lights, boundary sources,
black/quantized-alpha sources, ID transitions and independently anchored voxel
and SDF blocker fields. Omitting the destination clear is a negative control.
`BoundedLightVolumeMixedRadiusSchedulePreservesOverlapAfterEveryIteration`
runs the real schedule for overlapping r32/r8 and r30/r29 fixtures, checks the
bounded box after every residual-pair iteration, and keeps narrowest-first
order as a negative control.

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
