# Finite sun-face index capacity

Finite quads and supported analytic solids share one frame-local GPU index.
Two 1024×1024 sun cascades contain 32,768 tiles of 8×8 texels each. The first
64 candidate IDs per tile are inline. Up to 192 additional IDs use a shared
262,144-node spill pool; each node contains a record ID and the previous node
index. The complete query ceiling is 256 candidates, independent of unrelated
records elsewhere in the index. The record pool starts at 65,536 ten-word
records and grows between frames to the demand the previous frame measured
([Record-pool exhaustion](#record-pool-exhaustion)).

The appended spill counter, heads and nodes cost 2,228,228 bytes. An appended
512 KiB hierarchy plus two metadata words brings the default sun buffer to
30,670,868 bytes, including both depth layers, the two header words, records
and tile lists; each further record costs 48 bytes of geometry and hierarchy.
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

Reservations past the allocated capacity publish no geometry, but every lower
ID is a published record once the bake dispatches finish. The first failed
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
complete. Within one frame, which records are lost still follows bake-dispatch
order. Across frames the pool grows to the measured demand, so a scene whose
demand fits the ceiling loses records only on the frames before the growth
lands.

### Frame-latent capacity growth

The bake system copies the frame's requested-record count into a readback
slot after the hierarchy build and reads it once its submission has completed
(`GpuReadbackRing`; frame age proves nothing). Before the next clear, the
largest completed sample picks the smallest power of two that holds it, never
below the current capacity and never above the ceiling, and the buffer is
reallocated behind the same `Buffer` (`Buffer::resize`): queued frames keep
the old storage, every consumer rebinds through the object on its next
dispatch, and the allocator writes the capacity into the header word before
the first dispatch that reads it. GLSL recovers the capacity from the bound
length, keeping the header index runtime-derived for NVIDIA's cold link; Metal
reads the header word. Every word from the spill counter on is addressed
through the `sourceFace*Index` helpers, and the hierarchy build dispatches as
many cooperative passes as the leaf allocation needs (two up to 131,072
records, three up to the 16,777,216-record dispatch limit), publishing
readiness from the pass that roots the tree. The default allocation keeps its two-pass dispatch sequence
and its frames stay byte-identical: the frozen-ceiling matrix matched the
parent on all 41 captures.

Growth is monotonic within a run; the first frame whose demand exceeds the
pool loses records as before, the sample completes one frame later on Metal
and the grown pool serves the frame after that (filler spawned at render
frame 9, demand sampled at frame 10, applied at frame 11 in the transition
fixture). At the ceiling the exhaustion is reported once (`SUN-RECORD-POOL
exhausted at ceiling`) and the probe records the demand peak, the exhausted
samples and the capacity changes; the lost-tile policy above then holds.

The ceiling is chosen from measurement, not from a workload that might
appear. `IRPerfGrid`'s 262,144-voxel grid requests 786,432 records at zoom 1
and 4; letting the pool reach 1,048,576 records (77,856,788 bytes) recovers
every record but costs 1.62x (zoom 1) and 2.82x (zoom 4) of the steady frame,
because overflowed tiles then traverse an unsorted 786,432-record hierarchy
(`computeSunShadow` 0.05 ms to 12.2 / 39.5 ms, `computeLightVolume` 2.6 / 6.1
to 13.1 / 44.6 ms). That is the spatial-ordering and traversal cost the
hierarchy's own follow-up owns, not a cost of growth; it stays a measured
tradeoff for design review. The shipped ceiling is 131,072 records
(`kSourceFaceCapacityCeiling`, 33,816,596 bytes), the largest pool whose
IRPerfGrid cost stayed inside the 1.10x frame budget in the
[native controls](../pr-screenshots/claude/4759-sun-face-pool-exhaustion/README.md#frame-latent-capacity-growth)
(steady mean 1.058x and 0.989x at zoom 1 and 4; 262,144 reaches 1.35x at zoom 4);
`IRPrefab::SunShadow::configureRecordPool` sets another capacity or ceiling
for a creation or a diagnostic (`IRCanvasStress` / `IRPerfGrid`
`--sun-record-capacity` / `--sun-record-ceiling`).

### Candidates measured

| Family | Correctness | Latency | Memory | Cost | Decision |
|---|---|---|---|---|---|
| Capacity growth from completed demand samples | Every exhausted CanvasStress case (three finite filler-first, the 280 straddle, three analytic, the moving filler-first track) matches its sufficient-capacity control with 0 missing and 0 excess pixels outside the 2 px band; frames without exhaustion byte-identical | One exhausted frame per demand step | 48 bytes per record above the default; 33,816,596 bytes at 131,072 | +4.3% steady frame on the exhausted CanvasStress cell, 1.000x on its control; IRPerfGrid at the ceiling per the table above | Adopted, with a measured ceiling |
| Deterministic retention priority | Rejected by counting: a pool of C records over a receiver domain in which more than C records are each the sole blocker of some receiver loses a sole blocker under every order, and ordering only chooses which receiver reads lit. `test_render_sun_record_capacity.py` executes the counterexample through both dialects' producer, clear, hierarchy and receiver: 1,026 finite faces stacked in low/high pairs, each the only face inside its own receiver's throw window by the ray oracle (the low and high receivers of a column need disjoint depth windows), and one distant face that owns every receiver's nearest-depth sampled texel. Against 1,024 records, all 12 retention orders tried (creation, reverse, by depth either way, distant first, high first, six shuffles) leave at least 4 blocked receivers lit, every receiver whose blocker was lost among them, while the 2,048-record control shadows all 1,026. The witness rejects the family, not a scene: IRPerfGrid requests 786,432 records against the 131,072 ceiling, and how many of the 655,360 it loses are sole blockers depends on its geometry | n/a | n/a | n/a | Rejected |
| Depth-slab extrema for lost records | Depth-only interval existence is exact with slabs no wider than the throw, but the finite query is per receiver position: [`slab_extrema_witness.py`](../pr-screenshots/claude/4759-sun-face-pool-exhaustion/slab_extrema_witness.py) keeps a slab's minimum and maximum, both of which miss the receiver's footprint while the discarded middle face blocks it | n/a | 16 slabs of two words per texel: 268,435,456 bytes | n/a | Rejected |

[Native Metal controls](../pr-screenshots/claude/4759-sun-face-pool-exhaustion/README.md)
force exhaustion with a rigid filler plate created before or after the
casters, or at a chosen frame. Before growth, when the lower caster's own
records were lost (filler first, the 280 straddle, or an analytic sphere baked
after every canvas), the lower shadow's centre was missing: its depth survived
only in the nearest-depth sampled layer, behind the distant casters. Analytic
solids reached that limit regardless of creation order, because
`SHAPES_TO_TRIXEL` bakes them after every voxel canvas and their records are
the first lost. With growth these cases match their controls from the frame
after the demand sample lands. Raster-only casters in the primary layer had the
same nearest-depth loss, independent of pool capacity; the opt-in
[depth-slab extrema](#raster-only-primary-layer-depth-slab-extrema) retain
them behind a nearest word beyond the throw.

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
record exhaustion, and the pool grows to the measured demand. Records lost past
the ceiling or on the frame before a growth lands, unpublished hierarchy frames
and raster-only casters can still lose it. Increasing bias, blending or accepting every retained partial-list
hit does not restore the missing information.

The hierarchy retains finite face bounds and depth intervals, so it can skip
disjoint geometry without discarding a lower blocker. Remaining representation
work includes records lost past the ceiling and raster-only sampled layers. It must preserve the finite
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
   candidate reads and GPU query time before selecting a layout. The primary
   layer's depth-slab extrema below are that prototype for raster-only casters;
   finite geometry still needs its own retention.
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

### Raster-only primary layer: depth-slab extrema

With `kSunDepthSlabSlots` set (three is the smallest useful value; the shipped
layout is zero), the primary layer keeps, beside each texel's nearest word, that
many slots of (min, max) packed words. A slot owns one slab of 64 depth units, the receiver's
shadow throw, so the slab of a packed word is its quantized depth divided by
65,536. The first writer of a slab claims the first empty slot with a
compare-swap; later writers of that slab atomically minimize and maximize the
pair. A slot's slab never changes once claimed, so a stale load that sees a
claimed slot is final and a stale empty load is corrected by the swap's return.
A texel that receives more distinct slabs than slots sets a sticky overflow
bit on slot 0's max word. The clear resets every min to the empty sentinel and every
max to zero. Every primary producer writes through one helper
(`ir_sun_depth_slab_write`): the canvas-depth bake, its world-placed and
per-axis resolves, and the ceiling-cut surface writes of the box bake. The
indexed fallback layer stays nearest-only; its casters have exact records.

The receiver consults the slabs in exactly one case: the nearest word was
rejected as beyond the throw. A near or same-plane rejection leaves nothing
valid behind the nearest word, and that keeps every other frame byte-identical.
The window `(sunZ - bias - throw, sunZ - bias)` is one slab wide, so the slab
holding its far end and the next slab are the only slabs that can hold a
passing write; the far slab's max and the near slab's min decide existence
exactly for depth-decided (direct and surface) words, and the other two extrema
recover a splat whose plane test rejected its neighbour. Every candidate runs
the unchanged per-word predicate, so provenance rejects as before and no
candidate adds a shadow no write supports. An overflowed texel, or a throw that
differs from the slab width, answers from the nearest word alone.

What the representation does not do: an interior write of a window slab is
unseen when both of its slab's extrema are splats the receiver plane rejects.
The executable harness (`scripts/tests/test_render_sun_depth_slab.py`) sweeps
61,750 write sets per dialect through every permutation up to 24: 18,440 of
them recover a blocker behind a nearest word beyond the throw, 200 overflow and
keep the nearest-only answer, and 48 are that splat-extrema residual, all with
coverage-splat markers inside the window and none with direct or surface
words. The same sweep holds the nearest-two counterexample (writes at -120,
-110 and 2 for a receiver at 10) that rejects a second retained depth as a
general repair. Routing raster casters through the finite index was rejected
by counting before any prototype: one record per rasterized caster pixel, 225
splat texels each, against a 65,536-record pool.

Native Metal controls on the particle-registered legacy bake
([evidence](../pr-screenshots/claude/4799-sun-raster-depth-slabs/README.md))
grade the GRID floor under a lower GRID caster and a distant GRID stack beyond
the throw. The parent loses 2,347 shadow pixels under eight or 128 coincident
casters, under reversed creation order, at 84 and 114 units, and 2,428 under an
oblique sun; the slab build matches the lower-only control exactly in every
such case, in both receiver modes, through a seven-shot track that crosses the
throw boundary, and for re-voxelized casters. Scenes without a hidden blocker
are byte-identical to the parent, including the distant-only, in-window and
unlit controls. Two distant bands plus the lower caster and the receiver
overflow three slots on the texels they share and keep the parent's hole;
that is the documented capacity limit, chosen by the cost below.

Three slots grow the sun buffer from 30,670,864 to 81,002,512 bytes: 48 MiB
of slab words in six texel-indexed planes, cleared every frame. The planes
measured faster than per-texel (min, max) pairs for both the clear and the
bake; prefetching a splat row's words before deciding spilled registers and
measured slower; issuing the extrema atomics only when a plain load shows the
write moves an extremum (both only move one way, so the skip is exact) did not
move the bake, because the cost is the two loads per splat write against
master's single atomic, not atomic contention. A staged ablation on one binary
attributes the raster-route cost to the layout (buffer and clear) and the
writer; the receiver's slab lookup is free.

Five interleaved pairs per cell, three slots over master `a6b9953c3` (Apple M4 Max,
Metal; the record-pool growth that merged afterwards touches the finite route only):

| Cell | Frame mean | Frame p99 | GPU envelope |
|---|---|---|---|
| IRCanvasStress raster route, lower caster | 1.285x | 1.416x | 1.601x |
| IRCanvasStress raster route, 128 distant casters | 1.340x | 2.220x | 1.846x |
| IRCanvasStress default scene, raster route | 1.004x | 0.954x | 1.006x |
| IRPerfGrid zoom 1 | 1.035x | 1.036x | 1.091x |
| IRPerfGrid zoom 4 | 0.994x | 0.989x | 0.999x |


The shipped layout therefore compiles the layer out (`kSunDepthSlabSlots = 0`:
master's buffer layout, byte-identical bake and receiver) and a creation that
needs raster casters retained behind a distant one opts in with three or more
slots in the three layout twins. Two paths could bring the layer inside the
budget and are not attempted here: a workgroup-cooperative splat that reduces
the 16x16 canvas tile's box footprint in shared memory and writes each texel's
extrema once instead of once per caster pixel, and a generation-stamped slab
plane that retires the per-frame clear. Nearest-two is rejected by the
harness's three-depth counterexample; routing raster casters through the
finite index is rejected by counting against the record pool with the census
the probe prints: the default CanvasStress scene on the raster route touches
69,431 and 66,136 primary texels in its two cascades, above the 65,536-record
pool at one record per texel, before a record per caster pixel and its
225-texel splat footprint.

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
analytic/raster-only completeness, traversal cost at the grown pool sizes a
dense scene demands, engine resource integration, native OpenGL parity and
actual moving-scene qualification.
