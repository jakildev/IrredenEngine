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
512 KiB hierarchy plus two metadata words, 16 KiB of leaf-order buckets and a
256 KiB leaf order bring the default sun buffer to 30,949,396 bytes, including
both depth layers, the two header words, records and tile lists; each further
record costs 52 bytes of geometry, hierarchy and leaf order.
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

Leaves are groups of eight records in a complete binary heap, taken in a
spatial leaf order rather than record order. Every node stores conservative sun-U, sun-V and sun-depth bounds
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
Each pass binds an immutable parameter buffer; no per-frame CPU readback or
parameter allocation is needed. Only the second pass publishes the
dynamic power-of-two leaf count, after the root is complete. Old nodes remain
unreachable after clear or pool shrinkage.

### Leaf order

Records arrive in reservation order, which follows bake dispatch and atomics,
so eight consecutive records span the whole scene and every leaf overlaps
every query: a lit receiver tests the entire pool. The bake therefore builds
the tree twice. A first build in record order publishes nothing; its root only
supplies the extent of the record bounds. Three passes then counting-sort the
record IDs into a leaf order: each record's bounds centre maps to one of
16×16×16 cells of that extent, in sun-U, sun-V and sun-depth, numbered in
XYZ Morton order. A count pass adds every record to its cell's bucket (the
frame clear zeroes the 4,096 buckets), one 128-thread workgroup turns counts
into start offsets, and a scatter pass claims a slot per record. The second,
queryable build and the receiver traversal read leaf slot *i* as record
`order[i]`.

The order only moves records between leaves. A query is a boolean over every
record whose bounds overlap the point, so any permutation, including the
arbitrary order of atomics within a cell, returns the same answer; only the
traversal cost changes. An unbounded record and a NaN or out-of-extent
position fall into an edge cell. The first build costs its passes again, and
the order adds three dispatches; a full sort (Morton keys through the per-axis
overflow sorter) ordered more finely but cost 0.35 ms of GPU and 0.11 ms of
CPU submission per frame on the Windows GL host, against 0.07 ms and 0.02 ms
for the cell sort, and saved only 0.08 ms of query time. Measurements:
[per-axis yaw attribution, small dense scene](per-axis-yaw-gpu-attribution.md#small-dense-scene-gl-4722).

Only incomplete, in-bounds surface tiles can use the ready hierarchy. Complete
tiles keep their local index, including when unrelated faces exhaust the global
pool. A complete hierarchy miss suppresses only the indexed sampled layer;
independent raster casters remain eligible. Facing, strict finite throw and the
existing quantization bias are unchanged. Stackless traversal has no arbitrary
visit limit and short-circuits only on a valid blocker.

The [guarded prototype timings](../perf/sun-hierarchy-throughput/README.md)
measured synthetic receivers against unsorted and sorted pools; they do **not**
measure this engine schedule. The leaf order above is graded on a full scene on
the Windows GL host instead.

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
readiness from the pass that roots the tree. Every pass, the per-record order
passes included, folds its workgroup count into the shared 2D dispatch grid
(X capped at 1,024, the rest in Y) and the kernel flattens both axes, so the
dispatch limit needs 1,024 × 128 record groups rather than 131,072 in one axis.
The default allocation keeps its two-pass dispatch sequence
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
and 4; letting the pool reach 1,048,576 records (77,856,788 bytes before the
leaf order, 82,067,476 with it) recovers
every record but cost 1.62x (zoom 1) and 2.82x (zoom 4) of the steady frame,
because overflowed tiles then traversed an unsorted 786,432-record hierarchy
(`computeSunShadow` 0.05 ms to 12.2 / 39.5 ms, `computeLightVolume` 2.6 / 6.1
to 13.1 / 44.6 ms). That is the traversal cost the leaf order targets, not a
cost of growth; those ceiling measurements predate the leaf order and have not
been repeated with it. The shipped ceiling is 131,072 records
(`kSourceFaceCapacityCeiling`, 34,357,268 bytes with the leaf order), the largest pool whose
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
after the demand sample lands. Raster-only casters in the primary layer share
the nearest-depth loss, independent of pool capacity.

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
   candidate reads and GPU query time before selecting a layout.
3. Cover nearby and distant overlapping casters, duplicate/insertion-order
   invariance, exact throw boundaries, oblique light, mixed source/GRID/revoxelized
   modes, both cascades and pool exhaustion. Then sweep height in a running scene.

### Raster-only primary layer: measured decision

The raster-only route runs whenever a particle raster pass is registered:
`beginVoxelFaceCoverage` then publishes no finite index, every visible canvas
pixel is a primary caster, and raster receivers take the independent-layer
PCF path. The [native fixture](../pr-screenshots/claude/4799-raster-sun-depth-preservation/README.md)
(`IRCanvasStress --probe-particle-raster --probe-ray-casters … --sun-primary-probe`)
stacks GRID boxes on the shadowbox sun ray over a GRID floor: lower-only,
distant-only, stacked, reversed creation order, duplicate and three-depth
stacks, unlit pairs, cardinal and oblique light, both cascades, and a caster
climbing through the throw boundary. Its capture-frame probe reads the floor
ray's primary and fallback words per cascade with zero finite records, and
counts each caster's canvas pixels so a culled caster is never mistaken for a
collision. Stacking a caster 72 units up the ray replaces the lower box's
nearest word (depth −15) with the distant top (−87) in both cascades and
erases the whole floor shadow (4,939 cardinal, 5,408 oblique and 4,308
cascade-0 missing pixels outside a 2-pixel band) while both casters keep their
600 canvas pixels; distant-only stays lit and unlit pairs are identical.

| Family | Evidence | Outcome |
|---|---|---|
| Second retained depth (nearest two distinct quantized depths; the loser of the nearest slot's atomic min lands in the otherwise empty fallback layer; the opt-in `retainSecondPrimaryDepth_` selects the `c_bake_sun_shadow_map_second_depth` compile-time variant, so the production bake carries no switch) | native fixture, two-slot write adapter and receiver controls in both shader dialects | **Rejected.** Zero recovered pixels in every stacked case: a flat raster top already yields three quantized depths per texel (−15, −14.667, −14.333 from the trixel lattice), so the second slot always holds the distant caster's own neighbour. No extra storage, no extra clear, one extra atomic per write and one extra read per tap, measured at +0.40 ms (+12 %) on `bakeSunShadowMap` and +9 % on the raster scene's GPU frame; the default path is byte-identical to the parent in all 165 captures and within noise in timing (bake 1.017×, GPU frame 1.011×). Arbitrary nearest-K inherits the failure with K words per texel. |
| Depth-slab extrema | `test_render_sun_depth_slab.py`: 3.9 million exhaustive interval queries; the native sparse three-slot prototype ([branch evidence](https://github.com/jakildev/IrredenEngine/blob/ece131393858f732df072d2aea6deed72b18eea0/docs/pr-screenshots/claude/4799-sun-raster-depth-slabs/README.md), [guarded current-tree cost](../pr-screenshots/claude/4799-raster-sun-depth-preservation/slab-timing/README.md)) | Dense slabs are proof-rejected: depth-only existence is exact only when a slab is no wider than the query window (6,968 counterexamples when wider), and sixteen min/max slabs over the encoded range cost 256 MiB. The sparse layout keeps three (min, max) slots per texel, each owning one slab one throw (64 units) deep, claimed by compare-swap with a sticky overflow bit; the receiver reads them only when the nearest word is rejected as beyond the throw, and every candidate runs the unchanged per-word predicate. Natively it repairs every beyond-throw stack with at most three distinct slabs per texel to the lower-only control exactly (the parent loses 2,347 cardinal and 2,428 oblique pixels), through reversed creation order, 128 duplicates, re-voxelized casters and receiver, both receiver modes and a seven-shot track across the throw, and leaves the distant-only, in-window, unlit and overflowed scenes byte-identical. Residuals: a fourth distinct slab on a texel sets the overflow bit and keeps the nearest-only hole (the cardinal three- and four-band stacks keep all 2,347 missing pixels, the oblique two-band stack 2,344 of 2,436), and 48 of the 61,750 write sets per dialect in that branch's harness hide an interior write because both extrema of its slab are splats the receiver plane rejects (none with direct or surface words). The sun buffer grows from 30,670,864 to 81,002,512 bytes, 48 MiB cleared every frame. Under `ir-acquire benchmark` on the current tree the two small raster cells run 1.45× and 1.63× frame mean, 1.56× and 1.68× GPU envelope, with `bakeSunShadowMap` about 2×; the default raster scene and both IRPerfGrid zooms stay inside 1.10×. **Measured rejection (over budget)**; the branch ships the layer compiled out (`kSunDepthSlabSlots = 0`). |
| Finite-index routing | canvas pixel census | **Rejected.** Every visible pixel is a caster: the three-object fixture occupies 31,320 of 463,524 canvas pixels (48 % of the 65,536-record pool), the default scene 34,376, and a covered canvas 7.1 pools. Routing points as faces also contradicts the index's authored-face semantics. |

No candidate meets the adoption gate, so the primary layer keeps one nearest
depth per texel and both prototypes stay opt-in: the second retained depth
behind `retainSecondPrimaryDepth_` here, the slab layer behind
`kSunDepthSlabSlots` on its branch. A receiver needs the nearest write in
`(receiver − throw, receiver − bias)`. The nearest or any fixed-K nearest
writes do not carry it; slab extrema one throw deep do, up to their slot
capacity and the splat-extrema residual, so the slab family fails on cost, not
on coverage. That branch's staged ablation splits the raster-route cost
between the layout and its clear and the writer, whose two covering loads per
splat write replace one atomic; the receiver's slab lookup measures free.
Bringing the layer inside the budget is the follow-up's scope: a
workgroup-cooperative splat that writes each texel's extrema once per 16×16
canvas tile instead of once per caster pixel, and a generation-stamped slab
plane or a vectorised clear that retires the per-frame 48 MiB clear. Adoption
is decided after that measurement, against the same gate. A two-pass bake that anchors a second slot to each texel's farthest write
(`min{z : z > max − throw}`) would answer the floor receiver exactly at one
extra 8 MiB layer, a second bake dispatch per producer and three atomics per
write; it still loses an elevated receiver with two casters more than the
throw minus its elevation above it, so it is a redesign to escalate, not a
bounded repair.

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
