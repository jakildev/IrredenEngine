# Finite sun-face index capacity

Finite quads and supported analytic solids share one frame-local GPU index.
Two 1024×1024 sun cascades contain 32,768 tiles of 8×8 texels each. The first
64 candidate IDs per tile are inline. Up to 192 additional IDs use a shared
262,144-node spill pool; each node contains a record ID and the previous node
index. The complete query ceiling is 256 candidates, independent of unrelated
records elsewhere in the index. The record pool holds 65,536 ten-word records.

The appended spill counter, heads and nodes cost 2,228,228 bytes. The full sun
buffer is 30,146,568 bytes, including both depth layers, records and tile lists.
No extra buffer binding or GPU pass is required. Ordinary inline queries do not
read the spill counter or heads. Spill traversal has dependent reads and must
be measured separately from the fixed-list path.

## Publication and failure

The clear pass resets tile counts, spill heads and allocation counters. Old
records and nodes remain unreachable. Producers reserve a record, append its ID
to every overlapping tile, and publish spill links with an atomic head exchange.
Consumers run only after the bake dispatches and their existing memory barriers
complete; the head exchange alone does not publish a node to concurrent readers.

A tile is exact only when all its candidates are available. A count above 256
selects sampled fallback. Failed record or spill allocation raises the tile
counter above that ceiling with atomic max; later reservations cannot make it
complete again. No partial-list hit is accepted as an exact result.

If the shared spill pool is exhausted, **every spilled tile** uses fallback for
that frame, even tiles that obtained all their nodes. Otherwise allocation order
could decide which receivers get geometric coverage. Complete inline tiles
remain exact. Global record exhaustion invalidates each affected tile; it does
not invalidate complete tiles touched only by successfully published records.

The default-off small-pool reference remains a diagnostic. Production queries
never scan the global record pool. The independent non-indexed depth layer stays
eligible after an exact indexed miss. Out-of-map and unsupported receiver paths
retain their existing behavior.

## Evidence and limits

The producer and cooperative append tests execute both shader implementations
with exact capacity boundaries, concurrent reservations, guard regions, sticky
failure and stale-frame clears. Receiver tests cover independent ray geometry,
spill and inline IDs, partial-list rejection, global exhaustion and mutations
that break the completeness guards. The native clear/probe test checks pending
GPU writes and reports actual allocated capacity.

[Crowded eight-yaw captures](../pr-screenshots/codex/shadow-index-complete-tiles/README.md)
compare the production query with the previously validated complete reference.
This extends exact coverage at moderate overlap; it does not solve arbitrary
density, record-pool exhaustion, sampled receiver placement or cascade clipping.
Larger workloads need measured hierarchy or range-compaction work, not a larger
unconditional scan, blur or a shadow-bias adjustment.

### Overlapping casters and finite shadow range

Occlusion is a union of blockers inside the receiver's shadow-distance window.
Adding an opaque caster must not remove an existing blocker at a fixed receiver.
The two sampled depth layers are therefore tested independently, including their
range and splat-plane rejection, before a tap contributes its filter weight once.
Taking the minimum depth across layers before rejection loses valid blockers.

This does not recover depths lost **within** either layer. A single nearest-depth
sample cannot answer an arbitrary bounded interval query: if receiver depth is
10, a lower caster is at 2 and the throw limit is 100, adding a caster at -120
replaces 2 with -120; the stored caster is then too distant. Complete finite-face
queries still find the lower caster. Incomplete tiles and raster-only casters
can lose it. Increasing bias, blending or accepting every retained partial-list
hit does not restore the missing information.

Pending work is a measured representation/query change that retains the needed
depth intervals without unbounded global scans. It must preserve the finite
throw limit and allocation-order independence. The moving-block report also
needs native Windows scene reproduction; this mechanism alone does not identify
every visible speckle. [Sampler controls and scene evidence](../pr-screenshots/codex/overlapping-caster-shadow/README.md)
separate the corrected cross-layer case from that remaining limitation.

The [crowded overlap fixture](../pr-screenshots/codex/crowded-shadow-overlap/README.md)
now reproduces the remaining loss in native Metal. One and eight coincident
upper casters keep complete tiles and preserve the floor shadow; sixteen and
128 create incomplete tiles and lose coverage, without exhausting either global
pool. Nearby upper casters produce edge loss; distant ones additionally open a
central hole. Every distinct upper pose has an identical unlit floor control.
The fixture is deliberately a known-failing diagnostic, not a blessed image.

### Ordered follow-up

1. Run the public overlap diagnostic on Windows/OpenGL, then compare the moving
   scene with index-completeness witnesses. Do not infer a platform cause from
   the original report or certify the scene from the Metal reproduction.
2. Prototype a depth-preserving query representation against the complete finite
   geometry oracle. Depth-only interval existence and finite face/plane coverage
   are distinct requirements; preserve both. Measure bytes cleared, append work,
   candidate reads and GPU query time before selecting a layout.
3. Cover nearby and distant overlapping casters, duplicate/insertion-order
   invariance, exact throw boundaries, oblique light, mixed source/GRID/revoxelized
   modes, both cascades and pool exhaustion. Then sweep height in a running scene.

A bounded depth-slab prototype is worth evaluating for **depth-only** samples:
partition depth into slabs no wider than the query interval. A query's lower
boundary slab needs its maximum, its upper boundary slab its minimum, and fully
contained slabs need occupancy. Both endpoints cannot lie strictly inside one
slab, so these extrema answer interval existence without retaining every point.
Atomic min/max are independent of insertion order. Endpoint equality still needs
strict-boundary tests. A dense layout over the full encoded depth range can be
too expensive; include storage and clear bandwidth in the comparison.

That proof does not cover payload-dependent splat rejection, reconstructed plane
tests or a finite footprint away from the stored tap. Keeping only extrema may
still discard the valid candidate after another rejection. It is not a complete
fallback design. A spatial/depth hierarchy retaining finite geometry is the
alternative to evaluate; another nearest-K cache, an enlarged unconditional
scan, or accepting whichever partial records won allocation does not establish
the required invariant.

### Captured finite-quad hierarchy experiment

`IRCanvasStress --sun-face-geometry-probe` adds the allocated record pool to the
capture-frame tile CSV. The versioned comment fields preserve each of the ten
32-bit words as unsigned decimal integers, including floating-point payloads.
Only allocated records up to pool capacity are read, after the same GPU finish
as the tile counters. Ordinary captures and normal frames do not read the pool.
The requested and captured counts remain separate when allocation overflows.

`scripts/perf/sun_geometry_probe.py` loads this capture and builds an offline
median-split bounding-volume hierarchy in sun U/V/depth. Every finite quad is
retained, including duplicates. A node can be rejected when its closed U/V
bounds miss the query or its depth bounds do not intersect the receiver's open
throw/bias interval. Leaves still evaluate the finite footprint and plane depth;
a node overlap is never itself treated as a blocker. Rational arithmetic treats
the captured binary32 values exactly. An exhaustive quad query checks every
hierarchy answer; independent ray/triangle tests validate the footprint oracle.

This experiment is deliberately narrower than a GPU replacement. It rejects
analytic-solid records and exhausted record pools instead of dropping them.
The pool contains geometry admitted by the capture's cascade domains, not every
object in an unbounded world. The oracle does not reproduce shader rounding,
edge tolerances, cascade selection, sampled non-indexed layers or receiver
placement. A GPU hierarchy additionally needs conservative binary32 bounds,
construction/publication, allocation-failure handling and measured clear/build/
query costs. Its worst-case traversal is still linear: many overlapping bounds
whose finite faces miss a receiver require every leaf candidate. Report that
case alongside useful depth pruning; do not turn candidate reduction into an
unmeasured frame-rate claim.

The [native record experiment](../pr-screenshots/codex/sun-geometry-capture/README.md)
contains seven captured pools, 2,023 matching query results and probe-on/off
pixel controls. It preserves the lower blocker with 128 distant overlapping
casters, while the unchanged production image still has a hole. These candidate
counts select a GPU experiment; no GPU performance improvement is claimed.
