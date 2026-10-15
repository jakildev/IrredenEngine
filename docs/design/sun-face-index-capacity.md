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
