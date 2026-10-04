# Voxel dispatch occupancy

The voxel stages have six XY lanes per sample, preserving the two halves of
each visible face. On Metal, Z lanes can share a group across several whole low-density
voxels. This changes dispatch occupancy, not voxel subdivision, face orientation,
depth arbitration, or shadow geometry.

Let `P` be the Z lane count, `S >= 1` the samples per voxel, `N` the compacted
voxel count, and `V = max(floor(P / S), 1)` the Metal voxels per XY group. The compact
writer dispatches `G = ceil(N / V)` XY groups, capped at 1024 in X with overflow
in Y, and `ceil(S / P)` Z groups. Empty lists retain one guarded XY group.

For flattened XY group `h`, Z group `g`, and local Z lane `l`, define
`L = min(S, P)`, `q = floor(l / L)`, and `r = l mod L`. The lane addresses
voxel `h * V + q` and sample `g * P + r`. When `q >= V`, the lane is padding;
its sample is marked outside the domain and the ordinary sample guard rejects
it. The voxel-count guard also rejects padded XY rows and partial final groups.

For `S <= P`, each group contains `V` disjoint ranges of `S` lanes. Each pair
`(voxel, sample)` has the unique inverse `h = floor(voxel / V)`,
`q = voxel mod V`, `l = q * S + sample`. Remaining lanes cannot start another
voxel: for `P = 32, S = 9`, lanes 27–31 are padding, not a fourth voxel.
For `S >= P`, `V = 1`, `q = 0` and `r = l`. For `S > P`, the unique
sample inverse is `g = floor(sample / P)`,
`l = sample mod P`. The final Z group rejects its unused tail.

`ir_voxel_dispatch` owns the slice domain and lane recovery in each shader
backend. Cardinal visible lists use effective subdivision squared; the feeder
uses its capped edge squared; per-axis lists use one stored sample per face.
The compact finalizer and both voxel stages use the same domain helper. Feeder
list indexing remains reversed and per-axis list bindings remain independent.
No CPU count readback or extra dispatch is needed to select the packing.

`test_render_voxel_dispatch_packing.py` executes the actual compact finalizer,
writer, helpers and consumer prefixes as scalar C++ adapters for both backends.
It checks distinct visible/feeder/axis counts, all modes and axis routes, density
clamping, non-divisor densities, empty lists, partial groups and the 1024-group
row boundary. Mutations remove index strides and guards or select the wrong
list count/sample domain; the controls must reject each one. Native rendering
remains necessary: these scalar adapters do not execute the raster or GPU memory
ordering, and equivalent coverage alone does not establish visual correctness.

Packing selection requires native timing across dense and low-density cardinal
work, rotation and intermediate densities. Integer division, register pressure
and partial groups can offset fewer dispatch groups. Keep sample identity and
requested fidelity fixed when measuring; the earlier global packing experiment
is recorded in [the baseline report](../perf/voxel-dispatch-packing/README.md).

Physical width is backend-specific: OpenGL uses eight Z lanes and Metal uses
32. Both preserve the same sample domain. The
[native occupancy controls](../perf/voxel-workgroup-occupancy/README.md) support
Metal's 32-lane groups with return-to-baseline measurements and unchanged images.
A same-runner Linux head/base/head diagnostic found a repeatable 17% steady-frame
regression at effective subdivision four with 32-lane groups; eight-lane OpenGL
groups with the shared packing calculation still regress, including the direct
dense-index variant. OpenGL therefore retains `V = 1` at every density and
maps each lane to `(h, g * P + l)`. Its original count and sample guards reject
padding. The helper interface and sample domain remain shared; only scheduling
is backend-specific. Hardware OpenGL requires separate measurement.

## Metal pipeline and ordering constraints

The registered 2×3×32 group needs 192 threads. `getComputePipelineState` checks
the compiled state's `maxTotalThreadsPerThreadgroup` before caching it, rejects
an unsupported size in release builds too, and releases the rejected state.
Pipeline creation failure also throws before any state query. It cannot resize
the group independently: the compact writer and shader lane mapping share the
physical width. The cached dispatch path does not repeat the limit query.
Apple documents the [pipeline-specific limit](https://developer.apple.com/documentation/metal/mtlcomputepipelinestate/maxtotalthreadsperthreadgroup);
a device-family headline limit is not enough to establish kernel support.

`MetalGpuComputeDispatchTest.VoxelStagesFitCompiledThreadgroupLimits` compiles
all five voxel entry points on the actual device and records requested/available
thread counts. `test_render_metal_pipeline_limits.py` executes the production
creation method and registry against stub devices with diagnostic assertions
disabled. It covers the acceptance boundary, rejection/release, failed creation,
retry and caching; mutation controls must reject a missing or off-by-one guard.

The shared voxel-stage bodies have no threadgroup storage, group barriers or
SIMD communication. Their device atomic depth minimum and winner-index minimum
do not depend on which voxels share a group. Depth, election and color execute
in separate compute encoders over directly bound, tracked resources. Their
publication follows Metal's [resource synchronization contract](https://developer.apple.com/documentation/metal/resource-synchronization),
not an assumption that relaxed atomics publish unrelated payload writes.
Overflow/source-face append indices are atomic; later encoders consume the
records, and source-face sorting uses stable voxel/face keys, not append order.

Winner selection identifies a voxel, not necessarily a single invocation.
Overlapping taps or per-axis half-face lanes can retain identical writes to a
texel. Packing preserves that pre-existing write set; it does not establish a
formally single-writer color path. Removing redundant stores needs separate
coverage and performance controls.
