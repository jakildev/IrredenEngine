# Finite sun-face index capacity

Finite quads and supported analytic solids share one frame-local GPU index.
Two 1024×1024 sun cascades contain 32,768 tiles of 8×8 texels each. The first
64 candidate IDs per tile are inline. Up to 192 additional IDs use a shared
262,144-node spill pool; each node contains a record ID and the previous node
index. The complete query ceiling is 256 candidates, independent of unrelated
records elsewhere in the index. The record pool holds 65,536 ten-word records.

The appended spill counter, heads and nodes cost 2,228,228 bytes. An appended
512 KiB hierarchy plus two metadata words brings the full sun buffer to
30,670,864 bytes, including both depth layers, records and tile lists.
It shares the existing buffer binding; hierarchy construction adds GPU passes. Ordinary inline queries do not
read the spill counter or heads. Spill traversal has dependent reads and must
be measured separately from the fixed-list path.

## Publication and failure

The clear pass resets tile counts, spill heads and allocation counters. Old
records and nodes remain unreachable. Producers reserve a record, append its ID
to every overlapping tile, and publish spill links with an atomic head exchange.
Consumers run only after the bake dispatches and their existing memory barriers
complete; the head exchange alone does not publish a node to concurrent readers.

A tile is exact only when all its candidates are available. A count above 256
selects the hierarchy when ready, or sampled fallback otherwise. Failed spill
allocation raises the tile counter above that ceiling with atomic max. Failed
record allocation sets the counter's sticky lost-record bit (`0x80000000`)
with atomic or; retained appends stay below it because one tile receives at
most one append per retained record. Neither can be undone by later
reservations. No partial-list hit is accepted as an exact result.

If the shared spill pool is exhausted, **every spilled tile** uses the hierarchy
or sampled fallback for that frame, even tiles that obtained all their nodes. Otherwise allocation order
could decide which receivers get geometric coverage. Complete inline tiles
remain exact. Global record exhaustion is handled per tile; see
[Record-pool exhaustion](#record-pool-exhaustion).

The default-off small-pool reference remains a diagnostic. Production queries
prune the global record pool through the hierarchy only for incomplete tiles.
A rejection-heavy query can still visit every record; there is no truncated
traversal budget that silently changes a miss into a complete result. The independent non-indexed depth layer stays
eligible after an exact indexed miss. Out-of-map and unsupported receiver paths
retain their existing behavior.

## Overflow hierarchy

The first production route uses unsorted groups of eight records in a complete
binary heap. Every node stores conservative sun-U, sun-V and sun-depth bounds
in eight uint words. Empty leaves and unbounded nodes have explicit state;
unsupported arithmetic and analytical records propagate unbounded state, and
analytical leaves still use their existing shape predicate. Finite leaves use
the same quad containment and plane-depth function as inline/spill candidates.
This is projected face testing, not distance stepping through empty space.

The index sets a GPU flag on the first tile overflow or shared spill exhaustion.
The existing clear resets that flag and readiness. After all current-frame
casters, the bake system submits two cooperative passes with storage barriers
between them and before consumers. Each 128-thread workgroup uses an 8 KiB shared
heap: the first pass builds disjoint subtrees of up to 128 leaves, and one second
workgroup joins their roots. Every used node keeps its global binary-heap index.
Local reductions synchronize every level; no group polls another group. Kernels
skip work when neither overflow nor record loss occurred, or the pool is empty.
After record exhaustion they build over the retained prefix of 65,536 records.
Each pass binds an immutable parameter buffer; no per-frame CPU readback,
sorting, or parameter allocation is needed. Only the second pass publishes the
dynamic power-of-two leaf count, after the root is complete. Old nodes remain
unreachable after clear or pool shrinkage.

Only incomplete, in-bounds surface tiles can use the ready hierarchy. Complete
tiles keep their local index, including when unrelated faces exhaust the global
pool. A complete hierarchy miss suppresses only the indexed sampled layer;
independent raster casters remain eligible. Facing, strict finite throw and the
existing quantization bias are unchanged. Stackless traversal has no arbitrary
visit limit and short-circuits only on a valid blocker.

The [guarded prototype timings](../perf/sun-hierarchy-throughput/README.md)
support starting without spatial sorting. Those timings do **not** measure this
engine schedule: even empty builds still incur two dispatch submissions.
Full-scene parent/head measurements, native Windows execution and global-pool
scaling remain required before claiming a throughput gain.

## Record-pool exhaustion

Reservations past the 65,536th record publish no geometry, but every lower ID
is a published record once the bake dispatches finish. The first failed
reservation requests the hierarchy, which is then built over that retained
prefix. Each tile overlapped by a failed reservation carries the lost-record
bit:

- An incomplete tile without the bit retained all of its candidates, so it
  queries the ready hierarchy, and a miss is as exact as with a complete pool
  and suppresses the indexed sampled layer.
- A tile with the bit never queries the hierarchy and answers from the sampled
  layers, as when the pool was complete but the tile overflowed without a
  ready hierarchy. No retained-pool miss can certify it, and a traversal for
  hits alone can visit the whole retained pool per receiver: a dense
  262,144-voxel grid exhausts the pool every frame and loses records on nearly
  every tile; querying those tiles costs 15.8 ms of `computeSunShadow` per
  frame on Metal against 0.05 ms for sampled answers.

Complete inline tiles keep their local list. Every tile is therefore either
exact or answered by the sampled layers; incomplete storage is never certified
complete. Which records are lost still follows bake-dispatch order,
so a tile's exact or sampled treatment depends on that order.

[Native Metal controls](../pr-screenshots/claude/4759-sun-face-pool-exhaustion/README.md)
force exhaustion with a rigid filler plate created before or after the
casters. With the receiver's records retained, crowded cardinal and oblique
scenes, static or with the caster stack drifting across the receiver every
frame, match their sufficient-capacity controls exactly, where the parent lost
200–228 shadow pixels. When the lower caster's own records are lost, the hole
remains unchanged: its depth survives only in the nearest-depth sampled layer,
behind the distant casters. Analytic solids reach that limit regardless of
creation order, because `SHAPES_TO_TRIXEL` bakes them after every voxel canvas
and their records are the first lost. Recovering it needs either
dispatch-order-independent retention of receiver-relevant records, or capacity
that does not exhaust; neither a larger bias nor accepting partial lists
supplies that information. Raster-only casters in the primary layer share the
same nearest-depth loss, independent of pool capacity.

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
queries still find the lower caster. The hierarchy now extends that guarantee
to incomplete tiles whose records the pool retained, including during global
record exhaustion. Records lost to exhaustion, unpublished hierarchy frames and
raster-only casters can still lose it. Increasing bias, blending or accepting every retained partial-list
hit does not restore the missing information.

The hierarchy retains finite face bounds and depth intervals, so it can skip
disjoint geometry without discarding a lower blocker. Remaining representation
work includes records lost to an exhausted pool and raster-only sampled layers. It must preserve the finite
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

### GPU construction and traversal diagnostic

The [native GPU experiment](../perf/sun-hierarchy-gpu/README.md) constructs a
complete heap over consecutive groups of eight records and queries it with the
production finite-face predicate. Conservative binary32 bounds reuse the
existing projected-bound helper for U/V and affine depth; uncertifiable bounds
remain unbounded. Separate tracked-resource compute encoders publish each level
before its parent. Heap-index traversal needs no local stack and never treats
an unfinished scan as an exact miss.

All seven captured pools, both record orders and fast/safe Metal math modes
match exhaustive GPU queries and the captured exact oracle. Independent node
readbacks check enclosure. Synthetic controls retain the full 65,536-record
pool and the rejection-heavy linear worst case. This validates a diagnostic
construction path, not production adoption: oblique-light candidate counts are
much worse than the spatially partitioned offline tree.

The [spatial-ordering experiment](../perf/sun-spatial-hierarchy/README.md)
generates GPU Morton keys and reuses the per-axis overflow sorter without
changing its comparator or network. Sorting record IDs preserves the complete
geometry pool. Rebuilt leaf bounds and queries use the same permutation.
The known-octant control rejects all-zero keys, while readbacks validate the
permutation, sorted keys, sentinels and conservative bounds.

Spatial ordering reduces oblique face tests but increases them in several
cardinal fixtures. Separate reference-free query timings include a complete
key/range/sort/tree build measurement; the repeated coherent receiver grid is
not a frame-rate benchmark. Preserve the complete-tile fast path when
evaluating an overflow fallback. Production selection still needs mixed
analytic/raster-only completeness, lost-record recovery, engine resource
integration, native OpenGL parity and actual moving-scene qualification.
