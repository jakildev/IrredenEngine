# Voxel feeder accounting: what each count measures

**Status:** Reference. Describes the measurement boundaries as implemented by
#5009 (PR #5073). It changes no semantics and no gate.

The voxel feeder is the path from a write to a voxel's position or alpha, through
the pool's queues and caches, to the bytes the render stage uploads. Several
counters sit along that path, and each one measures a different thing. Reading
one as if it were another is how a correct fixture reports a wrong conclusion.
This page names each boundary, the symbol that implements it, the test that
proves it, and what a zero at that boundary does and does not show.

Fixture populations, the numeric gates and the reasons for them live in
[`static-residents.md`](static-residents.md) § "Fixtures and gates". This page
does not restate a threshold. Where a sentence below says "the gate", that
section is the definition.

## The boundaries, in the order a write meets them

| # | Boundary | Counts | Implemented by |
|---|---|---|---|
| 1 | Producer notification | Calls announcing a completed position or alpha write | `C_VoxelPool::markCullBoundsDirty`, `CullRebuildCounters::writeNotifications_` |
| 2 | Deduplicated pending ranges | Distinct spans the upload queue still holds | `C_VoxelPool::queuePositionRange`, `coalescePendingPositionRanges`, `kMaxPendingPositionRanges` |
| 3 | Submitted upload | Bytes handed to the position buffer | `flushPendingPositionRanges` and `detail::submitPositionRanges` (`system_voxel_to_trixel.hpp`) |
| 4 | Cardinal and world cache work | Cull-bound slots rebuilt, in total and by whole-cache sweep | `C_VoxelPool::rebuildChunkBounds`, `CullRebuildCounters` |

The component and the render stage are in
`engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp` and
`engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp`. The report
fields are in `engine/profile/include/irreden/profile/profile_report.hpp`.

### 1. Producer notifications

`markCullBoundsDirty(start, count)` increments `writeNotifications_` once per
call, before the per-chunk pending bit deduplicates the span and before any
cache consumes it. No later stage can hide a notification: a queue that
saturates, a pending bit that is already set, and a cache that rebuilds
everything all leave the count intact. Position and alpha writes both reach it
(`queuePositionRange` calls it before anything else, so a saturated upload queue
still notifies).

A zero here proves nothing wrote a position or alpha through the pool's
notifying mutators that frame. It does not prove the pool's contents are
unchanged: a raw write that never notified is invisible to every boundary
below, which is what the forbidden-writes rules in `static-residents.md`
exist to prevent.

Positive controls (`test/ecs/chunk_bounds_eviction_test.cpp`):
`APositionWriteGivesANonzeroWriteWitness` and
`AnAlphaOnlyWriteGivesANonzeroWriteWitness`.

### 2. Deduplicated pending ranges

`queuePositionRange` merges a span into the last queued one when they touch.
When the queue reaches `kMaxPendingPositionRanges` it is sorted and merged in
place; repeated and overlapping notifications therefore cost no capacity. Only a
queue still holding more than half the bound of disjoint ranges after that
merge sets the saturated flag, and the flusher then falls back to revisiting
every CPU-owned slot. GPU-transform-owned slots keep the transform prepass
output either way.

The queue length measures queued work after deduplication, never submitted
bytes. A count taken here can read small while the next flush submits a great
deal, because saturation discards the ranges and substitutes a scan. It can also
read large while repeated writes were all one span. The gate treats it as
supporting evidence only.

The fallback's "CPU-owned" category is a pool slot's transform-index ownership:
`flushStaticPositionRanges` uploads the slots the CPU produces and leaves the
slots the GPU transform prepass produces alone. It is not the static-resident
state in the ECS, and a count of one is not a count of the other.

Positive controls (`test/ecs/voxel_position_queue_test.cpp`):
`RepeatedWritesBetweenFlushesCostOneEntry`,
`TheBoundHoldsExactlyItsDisjointRangesAndOneMoreSaturates`,
`AFullQueueOfRepeatsCompactsInsteadOfSaturating` and
`ASaturatedQueueStillInvalidatesTheCullCaches`. Sparse uploads across the former
range limit: `TenThousandDisjointRangesSurviveRepeatedCatchUpWrites` (same
file) and `EditsAfterUploadQueueSaturationStillInvalidate`
(`chunk_bounds_eviction_test.cpp`).

### 3. Submitted upload

`flushPendingPositionRanges` is the only place bytes leave the queue. An empty
queue returns immediately. A saturated queue submits through the fallback scan
and clears. Otherwise it coalesces and submits one batch. `submitPositionRanges`
adds the submitted size to the frame's `framePositionBytes_`, and the profile
report prints it as "Position upload": run bytes, largest single frame, and the
frames that submitted any.

A zero here proves nothing was submitted to the position buffer that frame. It
does not prove no write happened (boundary 1 answers that), and it does not say
anything about the record or mask buffers, which have their own upload counters.

### 4. Cardinal and world cache work

Two caches derive from the notified writes: the cardinal chunk bounds the cull
reads, and the world-AABB cache the light occlusion grid refreshes from. Each
has its own per-chunk pending bit, so a chunk can be owed to one and already paid
to the other (`CardinalAndWorldConsumersArePendingIndependently`).

`rebuildChunkBounds` rebuilds only the pending chunks, with one exception. A
cardinal index change, the first cardinal frame after a continuous-yaw frame, a
chunk-count change and the first build each sweep the whole cardinal cache. A
sweep also pays the chunks writes left pending, so it consumes write
invalidations too. `CullRebuildCounters` therefore carries:

- `cardinalSlots_` and `worldSlots_`: slots rebuilt in total;
- `wholeCacheSlots_`: the part of that rebuilt by a sweep (the camera or full
  rebuild line of the report);
- `writeNotifications_`: boundary 1, kept separate on purpose.

The report subtracts nothing. Total slots minus whole-cache slots is not "the
write-driven part", because a sweep and write-driven work can cover the same
chunks, and it is not "zero writes" when it reads zero, because a sweep can have
absorbed them. Only the notification count answers whether a write happened.

Positive controls (`chunk_bounds_eviction_test.cpp`):

- camera-only rebuild: `ACameraOnlyRebuildLeavesTheWriteWitnessAtZero`;
- write-only rebuild: `APositionWriteGivesANonzeroWriteWitness` and
  `AnAlphaOnlyWriteGivesANonzeroWriteWitness`;
- a notified alpha write coincident with a full cardinal rebuild:
  `WritesKeepTheirWitnessOnAFrameThatTakesTheFullCardinalRebuild`;
- the two kinds of work stay separate:
  `RebuildCountersSeparateWholeCacheSweepsFromInvalidation`.

## Simultaneous causes, warm-up and reset timing

**Two causes in one frame.** A write and a camera change can land on the same
frame. The sweep covers the write's chunks, so the rebuilt-slot figures cannot
tell the two apart; the notification count still records the write. That is why
gate 3 requires a frame with both causes to keep its write evidence.

**Warm-up.** Settlement can finish while cache debt is still owed: the light
occlusion grid refreshes stale world boxes over several frames after the last
resident settles. A zero read inside that window reads the debt being paid, not
the steady state. `C_VoxelPool::pendingCullDebt` reports what each cache still
owes (it is O(chunks), for fixture start asserts and tests, never a per-frame
engine path). A cardinal chunk an owed whole-cache rebuild will cover is the
camera's debt and is not counted as write debt
(`CullDebtCountsWriteDebtUntilEachCacheConsumerPaysIt`).

**Reset on read.** `consumeCullRebuildCounters` returns the counters and zeroes
them. The one engine consumer is the `VOXEL_TO_TRIXEL_STAGE_1` tick, which folds
them into the run witness (`renderRunWitness()`) on every exit path of the tick,
so the slots a tick rebuilds and the writes it consumed count in that tick's
frame. The profile report and the fixture asserts read the witness; a reader
that called `consumeCullRebuildCounters` itself would zero what the tick has not
yet folded and take the report's numbers with it. One consumption is distributed
to every reader through the witness.

**Per-frame attribution.** `beginTick` calls `openPoolFrame`, which closes the
previous frame and opens the next. `closePoolFrame` folds the frame's position
bytes, rebuilt slots, whole-cache slots and write notifications into the run
totals, the largest single frame, and the count of frames that saw each. The
report closes the last open frame before it reads. A write is therefore
attributed to the frame whose stage tick consumed it, which is not necessarily
the update that produced it.

## Observation without scanning

Every figure above is a counter an engine-owned stage increments at the point of
work: once per notification, once per rebuilt chunk, once per submitted range.
There is no per-voxel telemetry and no scan of the settled population. The one
O(chunks) read is `pendingCullDebt`, used outside the frame path. A fixture that
needs a population-wide check reads it from the cache that owns it, not from the
entities.

## What each zero proves

| Reads zero | Proves | Does not prove |
|---|---|---|
| Write notifications | No notifying write reached the pool that frame | A raw write that bypassed the mutators did not happen |
| Pending queue length | Nothing is queued for upload now | Nothing was submitted this frame, or that nothing was queued before a saturating flush |
| Submitted position bytes | The position buffer received nothing | No write happened; the record and mask buffers were also untouched |
| Rebuilt slots (total) | Neither cache rebuilt anything | Nothing is owed: a cache can be clean because it already paid |
| Whole-cache slots | No camera or full rebuild ran | No write occurred, or that the total is write-driven |

## Submitted bytes are not upload time

Bytes and time are separate measurements. On Metal a partial buffer write costs
one staged copy per range (`MetalBufferImpl::stagePatch`,
`engine/render/src/metal/metal_buffer.cpp`), so the time follows the number of
ranges, not the bytes. PR #5073's mostly-static fixture shows a small byte
volume in very many ranges costing more per frame than a much larger volume in
one range: see the table in the
[PR #5073 body](https://github.com/jakildev/IrredenEngine/pull/5073). #3725
tracks the per-range cost.

The gate's byte bound witnesses that settled residents are not re-uploaded. It
does not bound time, and no gate reads it as if it did. The upload time of a
frame is read from the stage's timing rows, not inferred from the bytes
submitted.
