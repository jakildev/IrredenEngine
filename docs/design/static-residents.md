# Static residents: entities that cannot change stop costing per-update work

**Status:** design, awaiting the repository owner's merge. Nothing here is
implemented. #5009 implements the merged document and is held to the gates
in [§ Fixtures and gates](#fixtures-and-gates).

A resident entity exists in the world and is drawn but has no behaviour of
its own right now: a placed voxel, a piece of scenery. This document fixes
what it means for such an entity to be *static*, how it stops being walked
by the per-update systems, how it becomes dynamic again, and what every
system and reader may assume about it.

## Summary

- The owner of an entity declares it static with a tag component,
  `C_Static`. The engine never infers it. An entity nobody classified is
  dynamic and behaves and costs exactly as today.
- A static entity's pose inputs can no longer be written by any system:
  every system or query that declares a write to them stops matching
  static archetypes, on every registration lane, C++ or Lua. The write is
  impossible, not forbidden by convention.
- The existing systems then derive from those frozen inputs one more time,
  in pipeline order. When a round that opened after the inputs last changed
  is complete, the engine adds a second tag, `C_Settled`, and the deriving
  systems stop matching the entity. A change to the entity's inputs, or to
  the global modifiers, while it is settling restarts the wait. No second
  code path computes derived state: a settled entity holds exactly what the
  ordinary systems last wrote.
- Every engine mutator that can change a static entity's inputs reaches the
  entity and thaws or resettles it, including the `C_VoxelSetNew` methods,
  which find it through the owner id the set already stores.
- Becoming dynamic again removes both tags. Each transition is one
  archetype move, measured at 0.20 to 0.28 µs per entity.
- Two families read world-scoped inputs as well as the entity's own: the
  fog reveal evaluators and the LOD gate. They keep matching settled rows
  but return once per archetype node unless one of those inputs changed.
- No scheduler-level conditional skip is added.
- The ECS rules say per-frame work on a rendered entity is unconditional.
  This design needs four passages reworded or extended; the replacement
  text is in [§ Rule changes requested](#rule-changes-requested).

## Scope and the tree described

The document describes master `369cb6b06` (2026-11-12). Of the sibling
tickets, #5005 has landed (PR #5046: `IRPerfGrid` and `IRLuaPerfGrid`
register `UPDATE_VOXEL_SET_CHILDREN` in RENDER). #5007 is open as PR #5055
and has not landed; its batch-form `MODIFIER_DECAY` is compatible with this
design and is not assumed. #5006 and #5010 have not landed. The design does
not use the update-side cull gate (`C_VoxelPool::isRangeVisible`) at all for
static entities.

Out of scope: voxel record and active-mask uploads, the light occlusion
grid, GPU-side work, and any reduction in the bytes a resident occupies
(see [§ The bytes](#the-bytes)).

## Terms

| Term | Meaning |
|---|---|
| Pose inputs | `C_LocalTransform`, `C_Modifiers`, and the `CHILD_OF` parent link. |
| Derived pose | `C_WorldTransform` and `C_ResolvedFields`. |
| Derived state | Derived pose plus everything computed from it: pool global positions, the owner-id range, queued position ranges, cull bounds, the GRID-rotated raster, and the fog and LOD gates on the voxel set. |
| Pose writer | A system that declares a write to a pose input. |
| Deriving system | A system that maintains derived state. |
| Round | One completed dispatch of every deriving system registered in INPUT, UPDATE and RENDER, group by group in that order. A round spans the three pipelines; no pipeline's end closes one. |

## The mechanism

Two tag components in the system layer,
`engine/system/include/irreden/system/components/component_static.hpp`
beside `C_SystemEvent`, give an entity one of three states. They live there
and not in `engine/prefabs/` because the scheduler applies the matching rule
and has to name them; see [§ The matching policy](#the-matching-policy).
`IRPrefab::Static` includes them. The state is archetype membership and
nothing else. No system's per-row work reads a flag or compares a value. The
one value the feature keeps per entity, the settle stamp on `C_Static`, is
read only by the transition step, and only for settling rows (see
[§ Settle](#settle-settling-to-settled)).

| State | Tags | Pose writers match it | Deriving systems match it |
|---|---|---|---|
| Dynamic | none | yes | yes |
| Settling | `C_Static` | no | yes |
| Settled | `C_Static`, `C_Settled` | no | no, or gated |

`C_Static` is the owner's declaration. It carries one engine-owned field,
`std::uint64_t settleStamp_`, zero until the transition step first sees the
entity (the width is fixed under [§ Settle](#settle-settling-to-settled),
"Counter width"). `C_Settled` is empty and engine-owned: only the engine adds or removes
it, and it never exists without `C_Static`.

Why two tags and not one. Removing the pose writers and removing the
deriving systems cannot happen at the same instant. If both stopped
together, a write made after a deriving system's last dispatch would be
frozen in without ever being derived. If the deriving systems stopped later
on a timer while writers still ran, the same gap would open at the other
end. With two tags the writers stop first, the inputs are then fixed, and
the deriving systems run over them once in order. A settled entity is
therefore equal, field for field, to a dynamic twin whose writers stopped
at the same flush.

The same shape is already in the tree: `FOG_SUBJECT_ADOPT` matches entities
that lack `C_FogRevealed`, processes each once, and attaches the component
so the entity leaves its matched set.

## What makes an entity static

**Who declares it.** The code that owns the entity. Four ways, all
equivalent:

1. `C_Static{}` in the `createEntity` bundle. The entity is created in the
   settling state.
2. `IRPrefab::Static::freeze(entity)` on a live entity.
3. From Lua, `IRStatic.freeze(entity)`, or `C_Static = {}` in a
   `components` table.
4. A world snapshot that saved the entity static (see
   [World snapshots](#world-snapshots)).

**What it is a property of.** The entity, expressed as its archetype. It is
not a property of a region. Voxels a chunk owns directly
(`ChunkResidencySlot::poolAllocation_`) belong to no entity, are already
walked by no system, and are unchanged by this design.

**The default.** Dynamic. An entity without `C_Static` is matched by every
system that matches it today, and no per-row branch is added to any of
them.

**Eligibility.** The first implementation accepts an entity when all of
these hold:

- It is a root, or its parent is static. A static entity's world transform
  cannot change, so every ancestor must be static too.
- Its rotation mode is GRID, explicit or implicit. It owns no entity canvas
  and is not a canvas part.
- Its voxel sets are CPU-direct
  (`gpuTransformSlot_ == IRRender::kVoxelTransformStatic`), and it is
  neither a skeleton root nor a joint.

The second and third conditions limit what #5009 has to prove; they are not
required by the mechanism. Lifting either needs only the tests for that
class.

`freeze` on an ineligible entity changes nothing and returns the reason. An
entity created or loaded with `C_Static` that turns out ineligible has the
tag removed at its first round and is counted in the report's `refused`
witness. Either way the entity stays dynamic, which is always correct.

## Transitions

All transitions are structural changes and all are deferred: they take
effect at the next structural flush, which the scheduler runs at every
pipeline group boundary
(`.claude/rules/cpp-ecs.md` § "Deferred entity operations during tick").
They may be requested from the main thread or from a worker tick, from C++
or Lua, from a system, a command or init code.

### Freeze: dynamic to settling

`IRPrefab::Static::freeze(entity)` adds `C_Static` at the next flush. From
that flush no pose writer matches the entity. One archetype move.

### Settle: settling to settled

The engine adds `C_Settled`. The owner does not call anything. The rule is
that a settling entity is promoted only after a complete round that opened
after its inputs last changed. A round that was already open when they
changed never counts, however far it had got.

**Rounds.** A round is counted over *deriving groups*. A deriving group is
a pipeline group with at least one deriving-system member: `registerPipeline`
gives every system its own group, and `registerPipelineGroups` may put several
in one. Which systems are deriving systems is defined in
[§ The matching policy](#the-matching-policy). A round is one completion of
every deriving group in pipeline order (INPUT, then UPDATE, then RENDER, in
group order within each). The system manager counts rounds with two integers
and a cursor over the deriving groups. Each deriving group also has a
*pending set*: the ids of its deriving members, taken from pipeline
registration.

- The deriving groups of INPUT, UPDATE and RENDER form one sequence, and the
  cursor runs over that sequence. There is one cursor, not one per pipeline.
- `roundsOpened_` advances when a pipeline starts and no round is open. The
  cursor moves to the first deriving group of the sequence, which may sit in
  a later pipeline than the one starting, and that group's pending set is
  refilled. A pipeline start while a round is open changes nothing.
- After a group's dispatch has joined, and before that group's structural
  flush, the main thread checks the group. If it is the group under the
  cursor, every deriving member that dispatched in this pass leaves the
  pending set. A member that was off cadence stays in it. When the set is
  empty the cursor moves to the next deriving group and refills its set. A
  dispatch of a deriving group not under the cursor changes nothing.
- When the cursor passes the last deriving group of the sequence the round
  closes and `roundsCompleted_` becomes `roundsOpened_`. This is the only
  write to `roundsCompleted_`. The end of a pipeline is not a round boundary.

**Rounds span pipelines.** One frame of `runMainLoopFrame`
(`engine/world/include/irreden/world/main_loop_frame.hpp`) runs INPUT then
UPDATE once per due tick, then RENDER once
(`RenderManager::renderFrame`). A round opened at the frame's first INPUT
stays open through every UPDATE pass and into RENDER. With
`UPDATE_VOXEL_SET_CHILDREN` registered in RENDER, the last deriving group of
the sequence is in RENDER. So `roundsCompleted_` does not change at any
INPUT or UPDATE drain of that round, and only the join of the RENDER group
can close it.

A drain can therefore promote an entity whose stamp is `s` only after round
`s` has closed. Round `s` opened after the entity's inputs last changed, and
its cursor then passed every deriving group, the RENDER group included. A
drain at the end of an UPDATE pass can promote an entity only when its RENDER
derivation has already run over its current inputs. The drain at the end of
the RENDER pipeline is the first that sees the round closed. A later INPUT or
UPDATE drain can promote rows the budget held back, against the same closed
round. A drain never closes or advances a round.

The other dispatches in a frame do not move the cursor: INPUT before the
second UPDATE tick, and UPDATE passes after the cursor has moved into
RENDER. They re-derive a settling entity from the same inputs, because any
change to those inputs would restamp it. A frame with no due UPDATE tick
runs RENDER alone, and a frame held for the first key press runs INPUT and
RENDER without UPDATE. A deriving group in UPDATE then stays under the
cursor, and the round finishes in a later frame. A headless test that runs the pipelines in another order
gets the same result: a group not under the cursor counts for nothing.

The counters, the cursor and the pending sets are main-thread state. A
member of a multi-system group runs on a worker, but none of this is written
from its dispatch: it is written once per group, after `executePipeline`'s
fan-out returns (`engine/system/src/system_manager.cpp`, the `parallelFor`
over the due members). The order in which the members of one group finish,
concurrent or in the serial no-worker fallback, cannot change what any of
them derives. `validateAllPipelineGroups` rejects a multi-system group in
which two members write the same component or one writes a component another
reads, so no member reads another's output. A `PARALLEL_FOR` deriving system
is always alone in its group, because the same validator rejects it in a
multi-system group. It is counted after its own join.

A change to round membership abandons the open round: a pipeline is
registered, cleared or composed (`appendToPipeline`, the insert forms), or
`addSystemExcludeTag` makes a system deriving. No round completes from the
abandoned one, and the next pipeline start opens a new round. Settling
entities wait longer, which is never wrong.

**Counter width.** `roundsOpened_`, `roundsCompleted_`, `globalFloor_`,
`globalModifierRevision_` and `C_Static::settleStamp_` are all
`std::uint64_t`. They are compared with ordinary ordering and never reset. At
1,000 rounds a second they wrap after about 585 million years, so the design
has no wrap-safe comparison and no rollover rule. A 32-bit stamp was
rejected. At 60 rounds a second it wraps in about 2.3 years, and after the
wrap an old high floor or stamp would hold new residents indefinitely. A
32-bit stamp beside 64-bit counters would instead truncate when it was
stored. The extra 4 bytes per static resident are counted in
[§ The bytes](#the-bytes).

With every deriving system at cadence 1 a round is one frame that runs at
least one fixed update, so promotion follows freeze by one to two frames.
The round is defined over whatever the creation registered, so the rule
holds for `UPDATE_VOXEL_SET_CHILDREN` in UPDATE or in RENDER without naming
either placement.

**The settle stamp.** `C_Static::settleStamp_` holds the number of the first
round that must open after the entity's inputs last changed. It is always
written as `roundsOpened_ + 1`, read at the moment the change takes effect.
Writing it again is the restart. It is written for these events:

| Event | When the stamp is written |
|---|---|
| The entity enters the settling state: freeze, creation with `C_Static`, snapshot load, resettle, or `C_Settled` dropped by another archetype move | At the drain after the flush that moved it |
| A resettle-worthy mutation ([§ Engine mutators](#engine-mutators)) on an entity that is already settling | At the drain after the mutation |
| A deriving system holds the row (below) | At the drain after the dispatch that held it |

So a settling entity mutated after `PROPAGATE_TRANSFORM` or
`REBUILD_GRID_VOXELS` has dispatched, but before promotion, waits for a
round that opens after the mutation, and every deriving system runs again
over the new inputs. Resettle on a settling entity removes no tag, but it
still restarts the stamp, so it is not a no-op.

Every event is applied at a drain that runs after the flush or dispatch it
follows. The stamp therefore never predates the change. Taking it late only
adds rounds: a change made between frames is stamped at the first drain of
the next frame, one round later than necessary, and is never early.

**The global-modifier floor.** `C_ResolvedFields` also depends on the global
modifiers, which are not an input of any one entity. The global-modifier
mutators ([§ Engine mutators](#engine-mutators)) advance a monotonic
`globalModifierRevision_`. When the drain finds a revision it has not seen,
it sets one world-scoped integer, `globalFloor_ = roundsOpened_ + 1`. It does
this before it evaluates any promotion (step 2 of the drain, below), so no
row can be judged against the floor that predates the change. A
global change therefore restarts every settling entity at once, with no
per-entity write. That includes a push that lands after
`MODIFIER_RESOLVE_GLOBAL` has dispatched and before the promotion flush.
While a global modifier changes every update (a one-update global pushed
each update), nothing is promoted. Settling entities stay settling, cost what
dynamic ones cost, and are counted in the report's `held_by_global` witness.

**Promotion.** At the drain a settling entity is promoted when
`roundsCompleted_ >= max(settleStamp_, globalFloor_)`. The drain applies its
own structural changes before it returns: it queues them and runs one
structural flush, at the same safe point as the pipeline's final flush. So
no system runs between the decision and the move, and no mutation can land
in that gap. A mutation after the drain finds the entity settled and
resettles it.

**Holds.** A deriving system that could not finish for a row reports a hold
through `IRPrefab::Static::noteChanged(entity)`, the same call the mutators
use (per-worker staging, safe from a worker tick). The holds are:

- `SEED_STAGED_VOXELS`: the set is still staged (no canvas yet).
- The fog evaluators: the set is ghost-held at a pose other than its own.
  This happens only when an entity moved while hidden and was then frozen.
  Once a static entity has been seen at its pose, its ghost pose equals its
  pose and it is not held.

A held entity stays settling and is walked like a dynamic one. It is
counted in the report's `held` witness.

A deriving system's own call to a listed mutator also restarts the stamp.
The one that occurs is `attachToCanvas` from `SEED_STAGED_VOXELS`, which is a
no-op once a set is pool-resident (`system_seed_staged_voxels.hpp`, header
comment), so it costs at most one extra round and cannot repeat.
`growAllocation`, called by `REBUILD_GRID_VOXELS` during derivation, is not
a listed mutator.

**The drain.** `IRPrefab::Static::drain()` is one main-thread step. The
World runs it after the final structural flush of each pipeline (every
INPUT and UPDATE pass, and RENDER), and a headless test calls it where it
calls the pipelines. Running it after each pipeline only stamps changes
promptly. Whether a row can be promoted depends on `roundsCompleted_`, which
the drain reads and never writes ([Rounds span pipelines](#settle-settling-to-settled)).
In order, it:

1. Applies the staged change notes and holds. A note whose entity is gone
   or not static is dropped, and on a voxel-set note it clears
   `ownerStatic_`. A note whose entity is settled queues the removal of
   `C_Settled`. Every other note stamps its entity.
2. Compares the global-modifier revision and, on a new one, raises
   `globalFloor_`.
3. Walks the settling archetype nodes (`C_Static` without `C_Settled`).
   Rows whose stamp is zero are new: it stamps them, runs the eligibility
   check, and adopts the owner of every voxel set on them (below). Then it
   collects the rows that can be promoted, against the floor step 2 left.
4. Promotes, lowest stamp first, up to the budget.
5. Flushes the removals and additions it queued.

The order of steps 2 and 3 is the cutover for global changes. A push that
lands after `MODIFIER_RESOLVE_GLOBAL` has dispatched, or after the round has
closed, is seen at step 2 of the next drain, before any row is collected.
No system runs between steps 2 and 5, so no later push can land inside the
drain.

Its cost is proportional to the settling population and the staged notes. At
steady state that is zero rows. The walk reads the 8-byte stamp column, not
a hash table. A loop that never drains promotes nothing, which is always
correct.

**Owner adoption.** Today `UPDATE_VOXEL_SET_CHILDREN` sets
`C_VoxelSetNew::ownerEntityId_` lazily, and only for a set with voxels
(`system_update_voxel_set_children.hpp`, the `ownerEntityId_ ==
kNullEntity` branch). Step 3 adopts the owner of every set it stamps,
including an empty one, and sets the set's `ownerStatic_` bit (below). It
goes through one helper, `IRPrefab::Voxel::adoptOwner(set, entity)`, which
both callers share so the pool's owner-id range is registered exactly as
today. A mutation made before adoption is still covered, because the stamp
written in the same step names a round that opens after it. From adoption
on, every set of a static entity names its entity and has `ownerStatic_`
set, which is what the mutator route in
[§ Engine mutators](#engine-mutators) relies on.

**Budget.** Promotions per round are capped by
`C_StaticSettings::promotionsPerRound_`, a singleton component. The default
is 8,192, which is about 2.3 ms at the measured cost. Zero means no cap; a
loader sets it for the duration of a load and accepts one long frame.
Entities over the cap wait for the next round and are promoted lowest stamp
first.

### Thaw: static to dynamic

`IRPrefab::Static::thaw(entity)` removes both tags at the next flush. From
that flush every system matches the entity again. One archetype move,
uncapped: a caller that thaws is about to change the entity and must not
wait. Thawing an entity that is already dynamic is a no-op.

Thaw cascades to every static descendant, because a descendant's world
transform depends on the entity's.

### Resettle: settled to settling

`IRPrefab::Static::resettle(entity)` leaves `C_Static` and records a change
note. If the entity is settled, it also queues the removal of `C_Settled`,
so that the removal applies at the next group flush and not only at the
drain.
The drain then restamps it, it is derived again in the next round that
opens, and it is promoted by the ordinary rule. On an entity that is already
settling it removes nothing and only restarts the stamp. It is the operation
behind every engine mutator that changes a static entity's inputs once, and
it is the sanctioned follow-up to a raw write (see
[§ Forbidden writes](#forbidden-writes)). It cascades to static descendants
for the same reason thaw does.

Thaw and resettle enumerate the static descendants through the `CHILD_OF`
relation when they are called, and queue the removals for all of them in the
same call. The whole subtree changes state at one flush, so no descendant is
left settled for a pass under a parent that has started to move.

### Any other archetype move: settled to settling

`C_Settled` does not survive an archetype move it is not part of. Attaching
or detaching any other component on a settled entity (`C_LodTierOverride`,
a skeleton, a GPU transform slot, a fog component, a creation's own
component) leaves the entity settling, and the drain stamps it as a new
settling row. The rule is generic and sits at the entity layer: a component
whose trait `IREntity::kClearedByForeignMove<T>` is true is dropped from the
destination type of any move that does not name it. `C_Settled` is the only
such component. Each archetype node caches whether its type contains one,
so the cost on any other move is one boolean test.

The rule covers every attach and detach, by C++ or Lua, bundle or
`setComponent`, without a mutator having to remember it. If the new
component makes the entity ineligible (a skeleton, a GPU transform slot),
the drain's eligibility check then removes `C_Static`, and the entity
becomes dynamic.

### Engine mutators

A mutator that changes something a static entity's derived state depends on
does the transition itself. The caller never has to remember it
(`.claude/rules/cpp-ecs.md` § "System-owned invariants: encapsulate, don't
delegate to callers"). Each row names how the mutator learns the entity. No
signature changes. Where the mutator is a component method it calls
`IRPrefab::Static::noteChanged(id)` with an id the component already stores.
That call appends 8 bytes to the calling thread's staging slot and looks up
nothing. The drain does the lookup on the main thread. A `C_VoxelSetNew`
method makes the call only when its `ownerStatic_` bit is set, so a dynamic
entity's set pays one boolean test per mutation and no note.

| Mutator | Effect on a static target | How it reaches the entity |
|---|---|---|
| `IRPrefab::Modifier::push`, `pushFrameLocal`, `pushOneFrame`, `pushLambda`, `upsertBySource`, `upsertBySourceInPlace` | Thaw. A modifier needs resolving and decaying every update. | Its `EntityId` argument |
| `IRPrefab::Modifier::removeBySource`, including the pre-destroy sweep | Resettle each static entity whose vectors it shortened | It walks nodes through `IREntity::queryArchetypeNodesSimple` instead of `forEachComponent`, so it has `node->entities_`. Only nodes whose type contains `C_Static` compare vector lengths. |
| `IRPrefab::Modifier::pushGlobal`, `upsertBySourceGlobal`, the global half of `removeBySource`, an expiry in `GLOBAL_MODIFIER_DECAY` | Advance `globalModifierRevision_` | World-scoped. `GLOBAL_MODIFIER_DECAY` advances the revision only when its `erase` removed an entry. The decrement of `ticksRemaining_` changes no composed value. |
| `IREntity::setParent`, `clearParent` | Thaw | Its argument |
| Destroying a static entity's parent | Thaw the children | A pre-destroy hook, given the id |
| `IRPrefab::RotationMode::setMode` | Thaw | Its argument |
| Attaching a gizmo to the entity as its drag anchor | Thaw | Its argument |
| Allocating a GPU transform slot, attaching a skeleton, or any other component attach or detach | Settled to settling, then eligibility | [§ Any other archetype move](#any-other-archetype-move-settled-to-settling) |
| `C_VoxelSetNew` record, alpha and edit mutators: `changeVoxelColor`, `changeVoxelColorAll`, `changeVoxelPriority`, `changeVoxelPriorityAll`, `activateAll`, `deactivateAll`, `fillPlane`, `reshape`, `editVoxels`, `carve`, `resyncAfterRawEdits`, `resyncAfterAuthoredEdits` | Resettle | `ownerEntityId_`. Every one of them already ends with one entity-keyed call per mutation, `IRPrefab::VoxelPool::withPoolByEntity(canvasEntity_, …)` that bumps `markRecordsChanged()`. Most reach it through `markPoolRecordsChanged(first, count)`, directly or via `resyncDerivedState`. `changeVoxelPriority` and `changeVoxelPriorityAll` make the call inline. #5009 folds those sites into one private helper that also calls `noteChanged(ownerEntityId_)`. A new record mutator that bumps the generation therefore resettles too. |
| `C_VoxelSetNew` span and canvas changes: `attachToCanvas`, `detachToStaged` (the canvas-teardown re-stage) | Resettle | `ownerEntityId_` |
| `C_VoxelSetNew` LOD band: the new `setLodBand(min, max)`; the Lua `lodMin` / `lodMax` properties (`component_voxel_set_lua.hpp`), whose setters call it instead of assigning the fields | Resettle | `ownerEntityId_` |
| `IRPrefab::Fog::setHiddenPolicy`, `setSubjectClass`, `setEntityRevealGoverned`, and the new `setOverride`, `setChannels` | Resettle | Their `EntityId` argument |

**Why `ownerEntityId_` is a sound route.** The field is already in the
component. Owner adoption in the drain
([§ Settle](#settle-settling-to-settled)) makes it name the entity on every
set of every static entity.

The one new field is `bool ownerStatic_`. It sits beside `visible_`,
`ghostHeld_` and `lodCulled_`, in the padding byte before `lodMin_`, whose
`IRRender::LodLevel` is a `std::uint32_t` enum. So `sizeof(C_VoxelSetNew)`
stays 200, which the header's `static_assert` checks. Only the main-thread
drain writes the bit: it sets it at adoption, and clears it when it drops a
note because the entity is no longer static. The error can only go one way:

- A stale `true` after a thaw or refusal costs one note, which the drain
  drops before clearing the bit.
- A `false` on a static entity is possible only before its first drain, and
  the stamp written at that drain covers every earlier mutation.

It is a routing hint for the mutator. No system reads it, and no work is
skipped on it. Gate 6 bounds its cost on dynamic entities. The call follows
`engine/prefabs/CLAUDE.md` § "Component method rules": the component hands
a stored id to a prefab-scoped namespace, and only the namespace touches
the ECS. That is the same shape as the existing
`markPoolRecordsChanged` → `withPoolByEntity(canvasEntity_)` call.

**Migration.** None for callers: no mutator's signature changes. Inside the
engine, `C_VoxelSetNew` gains `setLodBand` and `ownerStatic_`. The Lua `lodMin` / `lodMax`
setters switch to it. `removeBySource` switches its walk to node iteration.
`UPDATE_VOXEL_SET_CHILDREN`'s owner adoption moves into `adoptOwner`.

An entity whose voxels a system rewrites every update (the colour
animation systems call `changeVoxelColorAll` per tick, for example
`system_animation_color.hpp`) is resettled at the first such call and then
restamped every update. It stays settling: correct, no cheaper than today,
and it costs exactly one archetype move. The `resettled` witness shows it.
To make such an entity cheap, the creation stops animating it or does not
declare it static.

### Cost per entity

Measured (Release, Apple M4 Max, headless, 2026-11-12, master `369cb6b06`,
1,000,000 entities with the million control's component set, three runs;
[`docs/perf/static-resident-design/structural-move-probe.txt`](../perf/static-resident-design/structural-move-probe.txt)):

| Operation | µs per entity |
|---|---:|
| Add one tag (queue plus flush) | 0.20 to 0.28 |
| Remove one tag (queue plus flush) | 0.06 to 0.07 |

The ticket's "microseconds per entity" was an estimate; the measured move
is roughly ten times cheaper. Freezing a live entity is two moves (freeze,
then settle), about 0.5 µs. Creating an entity static is one move beyond
creation. A million moves in one flush still take 200 to 275 ms (measured,
same runs), which is why promotion is capped per round.

The probe's tag is empty. `C_Static` carries the 8-byte stamp, which adds
under 2 % to the roughly 500 bytes each move copies (estimated from
[§ The bytes](#the-bytes), not measured).

## What each system and reader may assume

For every row below, "may assume" holds for a **settled** entity. A
**settling** entity is in the same position as a dynamic one: its derived
state is as current as the last dispatch made it.

### Deriving systems

A deriving system declares how it treats settled rows in one of two ways.

- **Excluded** (`Exclude<C_Settled>` in its registration). Every input it
  reads is the entity's own and is frozen while the entity is static.
- **Gated** (the access tag `IRSystem::GatesSettled`, batch-form tick). It
  also reads world-scoped inputs. It is dispatched every pass, and its tick
  returns once per settled node unless one of those inputs changed. When it
  does run it fans out inside its tick at the grain it uses today and
  carries the `MainThread` access tag, the shape the epic fixed for
  `MODIFIER_DECAY` (#5004 ledger, D2).

Three requirements apply to both kinds for **settling** rows, because no
later dispatch will make up for a skipped one:

1. The row is derived on every dispatch. A cull gate, a `renders()` gate or
   a stagger does not skip it. The one exception is a ghost-held set, which
   keeps its ghost positions and is held as described above.
2. The decision is made per archetype node, by a second registration over
   `C_Static` without `C_Settled` or by a batch-form node branch. No
   per-row "is it static" test is added to a dynamic row.
3. The system's own archetype queries (`PROPAGATE_TRANSFORM` and the fog
   evaluators query in `beginTick`) apply the same exclusion as its
   registration.

| System | Kind | May assume about a settled entity | Must not assume |
|---|---|---|---|
| `PROPAGATE_TRANSFORM` | Excluded | `C_WorldTransform` equals the composition of the entity's `C_LocalTransform`, its parent's `C_WorldTransform` and its modifier contribution as they stood when it settled, and none of the three has changed since. A settled parent is outside the candidate set and is read like any other non-candidate parent. | That a global modifier pushed after settling is folded in. It is not (see below). |
| `MODIFIER_RESOLVE_GLOBAL`, `_EXEMPT`, `_LAMBDA` | Excluded | `C_ResolvedFields` holds the values resolved from the entity's modifiers and the global modifiers current at its last round. | That the values track later global modifiers. |
| `MODIFIER_DECAY` | Pose writer | It never matches a static entity, settling or settled. A static entity's modifier lifetimes do not advance. | — |
| `UPDATE_VOXEL_SET_CHILDREN`, in UPDATE or RENDER | Excluded | For every set that is not ghost-held, each pool global position equals what `updateAsChild` writes for the current world translation. The owner-id range is registered. The position range was queued for upload once, when the entity settled, and is not queued again. This system invalidates no cull chunk for it. | That the range is in the viewport. The settling write ignores the cull gate. |
| `REBUILD_GRID_VOXELS`, `_IMPLICIT` | Excluded | The pool span holds the raster the rebuild emits for the current world rotation and source records. For identity rotation that is the authored span. | — |
| `SEED_STAGED_VOXELS` | Excluded | Every set of the entity is resident in a pool. | — |
| `FOG_REVEAL_EVAL` and its `_GHOST`, `_SHAPE`, `_SHAPE_GHOST`, `_CANVAS`, `_CANVAS_GHOST` variants; `FOG_SUBJECT_ADOPT*`, `FOG_SUBJECT_EXEMPT*` | Gated | The subject's world position is fixed. After every fixed update its `C_FogRevealed`, `C_FogGhost`, the set's `visible_` and `ghostHeld_`, the pool mask and the carrier bits are what the evaluator would have produced had it visited the subject, with the same stagger latency as today. | That a verdict survives a change on the fog side. Every input in the trigger table below arms the pass. |
| `GATE_VOXEL_SETS_BY_LOD` | Gated | `lodCulled_` and the pool mask match the entity's resolved tier as of the last change to the LOD snapshot. | — |

**Fog triggers.** A settled subject's verdict still changes when the fog
does. The gated evaluators run over settled rows when any of these inputs
changed since their last pass, and for `staggerPeriod_` consecutive
dispatches so each staggered row is visited once
(`.claude/rules/cpp-ecs.md` § "A change-gated recompute must trigger on
every input its function reads"). The list is the read set of
`system_fog_reveal_eval.hpp` on `369cb6b06`; the implementation re-reads it
and carries one test per row.

| Input | How the change is signalled |
|---|---|
| Field cells: state, channels, explored policy and clock | A revision counter on `C_CanvasFogOfWar`, advanced by each mutator that changes a cell's content |
| Vision sources: circles, line-of-sight flags, ceilings | The same counter, advanced by the `IRPrefab::Fog::` setters |
| The published line-of-sight field and its observers | The same counter, advanced when `FOG_LOS_BUILD` publishes |
| `C_FogRevealSettings` | A copy held on the system, compared once per dispatch |
| The active canvas, its pool, and whether fog is attached | Ids held on the system, compared once per dispatch |
| The subject's own `override_`, `channels_`, hidden policy and class | The setters resettle the entity; the evaluator then visits it as a settling row |

Region eviction and reload do not change cell content and are not a
trigger. An armed pass runs the same residency pre-pass
(`touchAnchorRegions`) as today before it reads.

While the fog changes every update, as it does when vision sources move,
the gated pass runs every update and costs what the evaluator costs today.
Bounding that by region is a follow-up; see
[§ Alternatives rejected](#alternatives-rejected).

**LOD triggers.** `C_ActiveLodLevel` is compared against a copy held on the
system. Attaching or detaching `C_LodTierOverride` on a settled entity
leaves it settling
([§ Any other archetype move](#any-other-archetype-move-settled-to-settling)).
A band change through `setLodBand` or the Lua `lodMin` / `lodMax`
properties resettles it. A C++ assignment to `lodMin_` / `lodMax_`, or to an
attached override's tier, is a raw write.

**Hidden, LOD-culled and ghost-held sets.** These gates are written only by
the fog evaluators and the LOD gate, which keep running when their inputs
change. Pool positions of a settled entity are true while it is hidden or
culled, so nothing has to be rewritten when it is shown again. A set that is
ghost-held at its own pose is in the same position.

### Systems that neither write pose inputs nor derive

A system that does neither is unchanged. It matches static entities as it
matches any other row, and its writes to its own components take effect.
`LIFETIME` still counts down and destroys a static entity; `PERIODIC_IDLE`
still advances a timer on one. To stop paying for such a system the
creation adds `Exclude<C_Static>` to it.

### Readers

| Reader | May assume about a settled entity |
|---|---|
| Collision systems that read `C_WorldTransform` | The world transform is current and did not change since the previous update, so the swept displacement is zero. A static entity is never a `COLLISION_RESPONSE` body (that system writes `C_LocalTransform`); it can be a solid. |
| Picking, CPU and GPU | Pool positions and the owner-id range are current. A static entity is picked and reported like any other. |
| Render stages | Nothing new. They read the pool, which holds true positions, and upload only queued ranges. |
| Cull-bound caches | A settled entity's chunks are invalidated when it settles and not again by any per-frame system. Mutators that already invalidate (`engine/prefabs/irreden/voxel/CLAUDE.md` § "Cull-bounds invalidation") still do. |

### World snapshots

`C_Static` is saved (`IR_SAVE_OPT_IN`). `C_Settled` is not
(`IR_SAVE_OPT_OUT`): it records that derived state is current, and
`loadWorld` restores CPU data only. A static entity therefore loads in the
settling state, is derived by the pipelines the creation already registers
for a load, and is promoted by the ordinary rule. A same-world double save
stays byte-identical because the saved set does not depend on whether the
entity had settled.

### Chunk residency

A static entity does not move, so it never migrates between chunks.
Eviction destroys it through the ordinary path. Freezing or thawing an
entity a chunk owns is a component write under
`engine/world/CLAUDE.md` § "Chunk mutation must route through
`markChunkDirty`", and the streaming code that makes the call marks the
chunk as it does for any other component write.

### Parent and child links

- A static child of a static parent: allowed. Both settle; the child is
  promoted no earlier than a round that composed the parent first.
- A dynamic child of a static parent: allowed. The child reads the parent's
  `C_WorldTransform`, which is current.
- A static child of a dynamic parent: not representable. `freeze` refuses
  the child. Thawing a parent thaws its static descendants.
- Reparenting a static entity thaws it.

## Forbidden writes

A write that would change a static entity's pose inputs reaches the entity
by one of three paths. None is silently lost.

| Path | Example | Outcome |
|---|---|---|
| A registered system or a query, on any lane in [§ The matching policy](#the-matching-policy) | `VELOCITY_3D` over an entity with `C_Velocity3D` | **Impossible.** A system that declares a write to a pose input does not match static archetypes, so the row is never visited. The entity does not move, and nothing is written. |
| An engine mutator | `IRPrefab::Modifier::push(entity, …)` | **Honoured.** The mutator thaws or resettles the entity as tabled above. A thaw, or a resettle through an `IRPrefab::` function, applies at the next flush like any structural change. A resettle from a component method (a `C_VoxelSetNew` mutator) applies at the drain that ends the pipeline. In both cases the entity is derived from the new inputs in the next round. |
| A raw reference | `IREntity::getComponent<C_LocalTransform>(entity).translation_ = p`, a Lua entity-handle field write, `forEachComponent` | **Retained, not derived.** The value is stored. Derived state does not change until the entity is next resettled or thawed, at which point it is derived from the then-current inputs. |

For the raw path:

- The sanctioned form is the raw write followed by
  `IRPrefab::Static::resettle(entity)` (once, after all writes), or `thaw`
  before sustained motion. This mirrors `resyncAfterRawEdits()` on
  `C_VoxelSetNew`.
- An assert build audits it. Each fixed update the audit recomposes a slice
  of the settled rows from their inputs and compares the result with the
  stored `C_WorldTransform` and the pool global positions, and it
  recomputes the LOD gate from `lodMin_`, `lodMax_` and the resolved tier
  and compares that with `lodCulled_`. Every settled row is covered at
  least once per 256 fixed updates. A mismatch is an
  `IR_ASSERT` naming the entity. The audit only asserts. It never skips or
  triggers work, so it is a validator and not a gate.
- A raw write to a subject's fog inputs (`override_`, `channels_`) on a
  settled entity is read by the next armed fog pass. It is delayed, not
  lost; the setters avoid the delay.
- A raw write to `C_WorldTransform` on a settled entity is the same case as
  a raw write to `C_LocalTransform`. On a dynamic entity it survives until
  the next `PROPAGATE_TRANSFORM`, as today.

## The matching policy

The rule that makes the system path impossible, where it lives, and how
every registration and query lane reaches the same answer.

**The rule.** A system or query does not match an archetype that contains
`C_Static` when either holds:

- it declares a write to a pose input (`C_LocalTransform`, `C_Modifiers`);
- it declares a write to derived pose (`C_WorldTransform`,
  `C_ResolvedFields`) and is not a deriving system.

A **deriving system** is one whose exclude archetype contains `C_Settled`,
or that carries the access tag `IRSystem::GatesSettled`. Both are explicit
in the registration, so membership is declared, not inferred from what the
system writes. A creation's own system that adds `Exclude<C_Settled>`
(migration step 3) becomes a deriving system. It joins the round, so an
entity cannot be promoted before that system has run over it. A deriving
system that declares a write to a pose input is a registration error: an
`IR_ASSERT` naming the system and the component, because the system would
exclude itself from the rows it must derive. Today `PROPAGATE_TRANSFORM`
(`registerSystem<PROPAGATE_TRANSFORM, C_LocalTransform, C_WorldTransform>`)
and `MODIFIER_RESOLVE_GLOBAL` (`createSystem<C_Modifiers, C_ResolvedFields,
…>`) list their inputs non-const. #5009 const-corrects both.

**Where it lives.** Each piece sits at the lowest layer that has the
information it needs. No layer reaches up, and nothing is installed at
startup.

| Piece | Layer and file | Why there |
|---|---|---|
| The guard of each component: `IREntity::kStaticGuard<T>`, an enum `NONE` / `INPUT` / `DERIVED`, primary template `NONE` | Entity, `engine/entity/include/irreden/entity/ir_entity_types.hpp`. The four pose components specialize it in their own headers under `engine/prefabs/irreden/common/components/`. | The prefab module states a fact about its own types and depends downward to state it. Every translation unit that names a pose component includes its header and sees the specialization, so the value cannot differ between units. |
| The guard per `ComponentId` | Entity. `EntityManager::registerComponent<T>` copies `kStaticGuard<T>` into a table indexed by `ComponentId`, beside the existing type cache. | `registerComponent<T>` is the only place that holds both `T` and the id. A `ComponentId` cannot exist without its guard. |
| `IREntity::kClearedByForeignMove<T>` | Entity, same header | The archetype-move rule above is a move-time rule |
| `C_Static`, `C_Settled` | System, `engine/system/include/irreden/system/components/component_static.hpp` | The exclusion adds `C_Static`'s id to exclude archetypes and the round tracker reads `C_Settled` in them. The system layer resolves both with `IREntity::getComponentType<…>()`. |
| The exclusion: one function, `IRSystem::detail::applyStaticExclusion(include, exclude, writes)` | System | Every lane below calls it. It is the only implementation. |
| Rounds | System, on `SystemManager` | Membership, the pending sets and the one cursor over the deriving groups of all three pipelines come from pipeline registration, and the cursor moves at each group's join inside `executePipeline` |
| Freeze, thaw, resettle, notes, drain, eligibility, `C_StaticSettings` | Prefab, `IRPrefab::Static` in `engine/prefabs/irreden/common/` | Eligibility reads rotation mode, GPU slots and skeletons, all of which are prefab knowledge |

**The lanes.** Each lane differs only in how it knows what the system
writes:

| Lane | Entry point | The write set passed to `applyStaticExclusion` |
|---|---|---|
| C++ system | `IRSystem::createSystem<Cs...>` | The `SystemAccess` it already derives: the non-const types in the pack plus `AlsoWrites<...>` (`system_access.hpp`, `applyComponent`). Computed at compile time. |
| C++ enum system | `IRSystem::registerSystem<N, Cs...>` | It calls `createSystem<Cs...>`, so it is the row above |
| CODEGEN Lua system | The generated `createSystem_<NAME>()` | It emits `IRSystem::createSystem<…>` (`cmake/lua_codegen/system_dsl.cpp`), so it is the first row. The emitter writes a `reads` component as `const IRComponents::C_…`. |
| EVAL Lua system | `IRSystem.registerSystem{ … }` in `engine/script/src/lua_script.cpp` → `SystemManager::createSystemDynamic` | Every listed component except those under the new `reads = { … }` key. `createSystemDynamic` gains a `readOnly` archetype parameter, and `setAt` on a read-only column raises. |
| Runtime C++ system | `IRSystem::createSystemDynamic` called directly | Every included component, minus `readOnly` |
| C++ query | `IRSystem::executeQuery<Cs...>` | The same compile-time derivation over its pack. It dispatches through an internal node walk, not through the public `executeQueryDynamic`, so the dynamic lane's conservative rule is not applied on top. |
| Runtime query | `IRSystem::executeQueryDynamic` | Every included component |

The function runs when a system is created and again whenever
`addSystemExcludeTag` changes its exclude archetype. So
`addSystemExcludeTag<C_Settled>` after creation turns a derived-pose writer
into a deriving system, and it stops being excluded from static rows. The
round tracker recomputes membership, and abandons the open round, whenever a
pipeline's composition or a member's exclude archetype changes
([§ Settle](#settle-settling-to-settled)).

**Initialization order.** None. The compiled lanes evaluate the trait at
compile time. The runtime lanes read the per-`ComponentId` table, which the
registration that creates each id writes, so a dynamic lane cannot name a
component that has no guard: Lua resolves names to ids only after the
component pack registered them. `C_Static`'s own id is resolved lazily at the
first exclusion. The same answer comes out for a system created before any
entity exists, for a Lua EVAL system created after static entities exist,
and for one created by hot reload. Components registered at runtime by name
(`registerComponentDynamic`, Lua-defined components) are `NONE`. None of
them is a pose component.

**Outside the policy: the raw entity layer.**
`IREntity::queryArchetypeNodesSimple`, `forEachComponent` and
`getComponent` apply no rule. A write through them is the raw path in the
table above: retained, audited, and applied by `resettle`. The deriving
systems that query in `beginTick` apply the exclusion themselves (requirement
3 under [§ Deriving systems](#deriving-systems)).

Twelve engine systems take `C_LocalTransform` by non-const reference and
four take `C_WorldTransform` that way (read from source, `369cb6b06`). Some
only read it, for example `system_contact_note_burst.hpp` and
`system_periodic_idle_note_burst.hpp`. Access is derived from the
registration pack, not from the lambda's parameters, so #5009 const-corrects
the pack and the parameter together for each one. That keeps them matching
static entities, and its PR lists each one.

## What the scheduler has to provide

**No conditional system skip.** The four questions the epic deferred to
this document (whether `endTick` and the tick observers run on a skipped
pass, whether it counts in `Calls`, what `getAccumulatedTicks` reports, how
an assert build keeps its audit) do not arise. An excluded system matches no
settled node, so there is no pass to skip. A gated system is dispatched on
every pass like any other system: `beginTick`, `endTick` and the observers
run, the pass counts in `Calls`, and cadence accounting is untouched. Only
its tick returns early, once per node. Measured for that shape in PR #5055
(Release, Metal, Apple M4 Max, million yaw sweep, 2026-11-12): about
0.02 ms per call for per-node dispatch over 51 archetypes, against 0.13 ms
per million rows for a per-row early return.

The scheduler gains three things:

1. **The matching policy**, as described under
   [§ The matching policy](#the-matching-policy): one exclusion function
   called from every registration and query lane, reading a guard the
   entity layer records per component.
2. **Round tracking.** `roundsOpened_` and `roundsCompleted_`
   (`std::uint64_t`), one cursor over the deriving groups of INPUT, UPDATE
   and RENDER as a single sequence, and each group's pending set ([§ Settle](#settle-settling-to-settled)),
   read through `IRSystem::roundsOpened()` and `IRSystem::roundsCompleted()`.
   `executePipeline` advances the cursor on the main thread after each
   group's dispatch joins, so grouped deriving systems need no shared
   counter.
3. **A `Walked` count.** The profile report's `Entities` column sums matched
   node lengths (`engine/system/src/system_manager.cpp`, the loop in
   `executeSystem`), not the rows a tick touched. A batch-form system
   reports the rows it iterated through `IRSystem::reportRowsWalked(n)`;
   the report prints them in a new `Walked` column. For a row-form system
   `Walked` equals `Entities`. A batch-form system that reports nothing is
   counted at its matched length.

## How it fits the ECS rules

| Part of the design | Rule relied on |
|---|---|
| The two tags route entities out of systems | `engine/system/CLAUDE.md` § "Registration and tick forms" (`Exclude<...>`); `engine/prefabs/irreden/common/CLAUDE.md` § "Modifier framework": exemption "is implemented by include/exclude archetype routing, not per-row branching" |
| Static-ness is declared, never inferred from values | `.claude/rules/cpp-ecs.md` § "A snapshot-compare-and-early-return is a dirty flag in disguise" |
| Freeze, thaw, resettle and promotion are queued and applied at a flush | `.claude/rules/cpp-ecs.md` § "Deferred entity operations during tick"; § "No dirty flags on components", pattern 1 (the mutator queues, the owner flushes once) |
| A settling set's positions are queued once | `.claude/rules/cpp-ecs.md` § "No dirty flags on components", pattern 1 (`C_VoxelPool::queuePositionRange`) |
| Mutators thaw or resettle; no caller-side resync step | `.claude/rules/cpp-ecs.md` § "System-owned invariants: encapsulate, don't delegate to callers", route 1 |
| `C_VoxelSetNew` methods reach their entity by handing the stored `ownerEntityId_` to `IRPrefab::Static::noteChanged`, which only appends it, and the drain does the lookup. `ownerStatic_` routes the call; it gates no work. | `engine/prefabs/CLAUDE.md` § "Component method rules": the lookup lives in a prefab-scoped namespace, not in the component. The same shape as the existing `markPoolRecordsChanged` → `IRPrefab::VoxelPool::withPoolByEntity(canvasEntity_)` call. |
| The settle stamp and the global floor are round numbers written when a change is applied and compared by the transition step. No deriving system reads them, and nothing clears them. | `.claude/rules/cpp-ecs.md` § "No dirty flags on components", pattern 1 (the mutator queues, the owner flushes); § "A change-gated recompute must trigger on every input its function reads" (a monotonic value compared with the last one seen) |
| The guard is a trait on the component, recorded when its id is created; the tags sit in the system layer that applies the rule | `engine/CLAUDE.md`: "keep dependency direction clear: low-level modules … do not depend on high-level ones" |
| `C_Settled` never exists without `C_Static`; every failure path removes both or neither | `.claude/rules/cpp-ecs.md` § "Multi-field honest gates must stay consistent across failure paths" |
| The gated fog and LOD passes. The fog revision is a monotonic counter the mutators advance; each consumer remembers the value it last saw and nothing clears it, so it is a change signal and not a dirty flag | `.claude/rules/cpp-ecs.md` § "A change-gated recompute must trigger on every input its function reads" |
| Cull bounds follow the settle-time write | `engine/prefabs/irreden/voxel/CLAUDE.md` § "Cull-bounds invalidation" |
| The settle-time position write ignores the render viewport | `engine/prefabs/irreden/voxel/CLAUDE.md` § "Transform and revoxelization pipeline", last paragraph (a narrower viewport freezes off-screen casters) |

### Rule changes requested

The owner decides these with this document. The edits themselves land with
#5009, inside the instruction-size budgets.
`engine/prefabs/irreden/voxel/CLAUDE.md` is already over its budget on
`369cb6b06` (210 lines against 200, read from
`scripts/lint_instruction_size.py`), so change 4 needs a trim in the same
edit.

**1. `.claude/rules/cpp-ecs.md` § "A snapshot-compare-and-early-return is a
dirty flag in disguise", last sentence.** Current:

> Per-frame work on a rendered entity is unconditional; the only honest
> skip is that the result is not observable this frame — gate on
> **visibility / cull** (`C_VoxelPool::isRangeVisible`), never on
> input-equality.

Replacement:

> Per-frame work on a rendered entity is unconditional, with two honest
> skips. The result is not observable this frame: gate on **visibility /
> cull** (`C_VoxelPool::isRangeVisible`). Or the entity is a settled static
> resident: its owner declared its pose inputs fixed and the engine has
> derived from them, so deriving systems exclude it by archetype
> (`C_Settled`, `docs/design/static-residents.md`). Never skip on
> input-equality.

**2. `.claude/rules/cpp-ecs.md` § "A change-gated recompute must trigger on
every input its function reads", appended paragraph.** The fog and LOD
gates read two world-scoped inputs that have no mutator to push from: a
settings singleton that creations write through a raw reference, and the
active canvas. New text:

> A trigger is pushed by the mutator that changes the input: a queued
> index, or a monotonic revision that each consumer compares with the value
> it last saw and that nothing clears. For a world-scoped input with no
> mutator to push from (a settings singleton, the active canvas id), the
> gated system may hold its own copy and compare it once per dispatch. The
> copy lives on the system, is O(1) in size, and is never per entity or on
> a component.

**3. `.claude/rules/cpp-ecs.md` § "System-owned invariants", route 2,
appended sentence.**

> A settled static resident is outside that schedule by archetype, not by a
> cached snapshot (`docs/design/static-residents.md`).

**4. `engine/prefabs/irreden/voxel/CLAUDE.md` § "Transform and
revoxelization pipeline".** "Visible sets upload every tick; only the
shared shadow-feeder cull gate skips work." becomes:

> Visible sets of dynamic and settling entities upload every tick; only the
> shared shadow-feeder cull gate skips work. A settled static entity's sets
> are written and queued once when it settles
> (`docs/design/static-residents.md`).

"On-screen rotation re-rasterizes every frame; transform comparison is a
forbidden dirty flag." becomes:

> On-screen rotation of a dynamic entity re-rasterizes every frame;
> transform comparison is a forbidden dirty flag. A settled static entity
> is rasterized once when it settles.

If the owner declines change 2, the two inputs get setters
(`IRPrefab::Fog::setRevealSettings`, and a bump where the active canvas is
set) and a raw write to the settings singleton joins the raw-write row of
[§ Forbidden writes](#forbidden-writes).

## The bytes

Per settled single-voxel resident with the million control's component set.

| | Before | After | Source |
|---|---:|---:|---|
| `C_VoxelSetNew` | 200 | 200 | Measured `sizeof`, this tree (192 in the 2026-10-04 registration log the ticket cites) |
| `C_Modifiers` | 72 | 72 | Measured `sizeof` |
| `C_LocalTransform` | 44 | 44 | Measured `sizeof` |
| `C_WorldTransform` | 40 | 40 | Measured `sizeof` |
| `C_FogRevealed` | 12 | 12 | Measured `sizeof`; `static_assert` in the header |
| Pool, per voxel | 68 | 68 | Read from source (`component_voxel_pool.hpp`: owner id 8, local position 16, offset 12, global position 16, record 12, transform index 4) |
| Entity id column | 8 | 8 | Measured `sizeof` |
| Entity index entry | about 54 | about 54 | Estimated: the ticket's 490-byte total less its itemised sizes; not re-measured |
| `C_Static`, `C_Settled` columns | 0 | 9 | Read from source: a column is a `std::vector` of the component; `C_Static` holds the 8-byte `settleStamp_`, and the empty `C_Settled` occupies one byte |
| `C_VoxelSetNew::ownerEntityId_`, `ownerStatic_` | (in the 200) | (in the 200) | Read from source: an existing field, now written for every set of a static entity, and one bool in an existing padding byte; the header's `static_assert` holds the size |
| **Resident in memory** | **about 498** | **about 507** | Sum |
| **Read by the GPU** | 36 | 36 | Read from source: position 16, record 12, id 8 |
| **Streamed per fixed update** | about 760 | 0 | Before: the ticket's derived estimate for the four systems. After: no deriving system or pose writer matches the row. |
| **Position bytes uploaded per rendered frame** | 16 | 0 | Read from source: one `VoxelGpuPosition` per voxel, re-queued by every `UPDATE_VOXEL_SET_CHILDREN` dispatch today |

While a gated fog pass is armed it streams the evaluator's own read set for
that update, as today. Beyond the stamp, the transition step's only state
is transient: 8 bytes per staged change note until the next drain, and a
few integers for the rounds and the global floor.

Memory per resident does not fall, and this design does not try to make it
fall. The two levers that would are named in
[§ Alternatives rejected](#alternatives-rejected).

## Public contract changes

Listed for the owner's approval. Every one is additive: a creation that
attaches no `C_Static` sees no change in behaviour.

| Change | Kind |
|---|---|
| `C_Static`, `C_Settled` in `engine/system/include/irreden/system/components/`; `C_StaticSettings` in `engine/prefabs/irreden/common/components/` | New components. `C_Static` is saved with no fields (the stamp is not saved); the other two are not saved. |
| `IREntity::kStaticGuard<T>` and `kClearedByForeignMove<T>` traits; the per-`ComponentId` guard table | New entity-layer surface. The four pose components specialize the guard. |
| `IRPrefab::Static::freeze`, `thaw`, `resettle`, `noteChanged`, `drain`, `isStatic`, `isSettled`, `stats` | New prefab namespace. `drain` is called by the World. |
| `IRStatic` Lua table with `freeze`, `thaw`, `resettle`, `isStatic`, `isSettled`, `stats`; `reads = { … }` on Lua `registerSystem` | New Lua surface. |
| `IRSystem::GatesSettled` access tag; `IRSystem::reportRowsWalked`; `IRSystem::roundsOpened`, `roundsCompleted`; a `readOnly` archetype parameter on `createSystemDynamic` | New scheduler surface. |
| `C_VoxelSetNew::setLodBand`, `C_VoxelSetNew::ownerStatic_`; `IRPrefab::Voxel::adoptOwner` | New method, field and helper. The `lodMin_` / `lodMax_` fields stay public. |
| `IRPrefab::Fog::setOverride`, `setChannels` | New setters. The fields stay public. |
| Systems that write pose inputs do not match static archetypes | New matching rule. It affects only entities that carry `C_Static`. |
| The mutators in [§ Engine mutators](#engine-mutators) thaw or resettle a static target | New behaviour for static targets only. |
| Global modifiers pushed after an entity settles do not reach it | New, documented limit. A settled entity's `C_ResolvedFields` reflect the global modifiers as they stood when the round that promoted it opened, and no global changed between that and its promotion (the global floor). A creation that needs a later global to apply thaws the entity. An assert build logs once per field when a global modifier is pushed while settled entities exist. |
| `Walked` column and a `Static residents` section in the profile report | Report format. `scripts/perf/` parsers and `test_profile_parser.py` change with it. |
| `createEntity` | **Unchanged.** It still attaches `C_LocalTransform` and `C_WorldTransform` to every entity, static or not. `C_Static{}` in the bundle is an ordinary component. |

Nothing is deprecated or removed.

**Migration.** None is required. A creation that adopts the feature:

1. Attaches `C_Static` to its scenery at creation, or freezes it later.
2. Thaws an entity before moving it, or uses the mutators, which thaw it.
3. Adds `Exclude<C_Settled>` to its own systems that only maintain state
   derived from pose, and `Exclude<C_Static>` to those that should ignore
   scenery altogether. Until it does, those systems keep visiting static
   entities, which is correct and costs what it costs today.
4. Declares read-only pose parameters `const` (C++) or under `reads` (Lua)
   in systems that must still see static entities.

## Fixtures and gates

### Fixtures

All four are `IRPerfGrid` modes; #5009 adds the flags.

- **Million control.** Unchanged command:
  `IRPerfGrid --wave-freeze --no-overlay --pivot-origin --auto-profile 300
  --config-preset configs/perf/million-profiling-off.lua --yaw 0 --yaw-step
  0.020943951`. Under `--wave-freeze` every cell is created with
  `C_Static`, and the demo sets `promotionsPerRound_` to zero while it
  spawns. `--no-static` creates the same cells dynamic; it is the
  all-dynamic twin of every fixture.
- **Mostly-static million** (the fixture with a moving fraction). The
  control's command plus `--moving-fraction 0.01`: every hundredth cell by
  index is dynamic and carries the per-cell wave; the other 990,000 are
  static.
- **Static cycle** (the render fixture). `--wave-freeze --static-cycle` at
  the render-verify grid size. Keyed on the fixed-update count, every
  hundredth cell is thawed, has its local translation raised by two cells,
  and is frozen again. With `--no-static` only the move happens. The live
  wave is not used here because it is not byte-identical between runs.
- **Recolored dynamic million** (gate 6). The control's command with
  `--no-static --recolor-every-update`: every cell is dynamic, and
  `changeVoxelColorAll` runs on every cell each update. It bounds what the
  mutator route costs an entity that is not static.

Each fixture asserts, when its profile window or its first capture begins,
that no entity is settling and that its script has finished. The profile
report prints the `Static residents` witnesses: `static`, `settling`,
`settled`, `promoted`, `thawed`, `resettled`, `restarted`, `held`,
`held_by_global`, `refused`, `promotion_ms_max`, `drain_ms_max`, position bytes uploaded per frame, and cull-bound slots
rebuilt per frame.

### Gates

#5009 carries seven default gates. Each is adopted or replaced here; the
replaced wording is the gate.

| # | Default | Decision |
|---|---|---|
| 1 | Each of the four systems visits under 1% of the base arm's entities per call | **Replaced.** |
| 2 | The four systems' time per rendered frame is at most 10% of base | **Adopted**, with the sum defined below. |
| 3 | Zero position bytes uploaded and zero cull-bound slots rebuilt per frame after the first frame | **Replaced.** |
| 4 | Static, dynamic, moved, static matches an always-dynamic twin; the mostly-static fixture renders identically to the all-dynamic scene | **Adopted**, extended. |
| 5 | A test demonstrates the documented outcome of a forbidden write | **Adopted**, one test per path. |
| 6 | With every cell moving, the four systems cost no more than base beyond noise | **Adopted** as written. |
| 7 | Render and lighting regression pass unchanged | **Adopted** as written. |

**Gate 1, rows not walked.** Million control, one run, any build type. Over
the profile window, per call and in the pipeline the fixture registers each
system in: the `Entities` column of `PropagateTransform`,
`UpdateVoxelSetChildren` and `ModifierDecay`, and the `Walked` column of
`FogRevealEval` and of every other gated system, are each under 1% of the
base arm's `Entities` for the same row. Reason for the replacement:
`Entities` counts matched rows, and a gated system matches rows it does not
walk.

**Gate 2, time.** Million control, Release, at least three interleaved
base and head rounds through `scripts/perf/repeat_profile.py` under the
benchmark lease, every run `RESULT=CLEAN` with `QUIET=guarded`. The sum is
per-call mean times calls divided by rendered frames, over
`PropagateTransform`, `UpdateVoxelSetChildren`, `FogRevealEval` and
`ModifierDecay`, plus on the head arm every row this design adds. Head is
at most 10% of base. Noise bound: the base arm's round range for the sum in
the same session.

**Gate 3, nothing re-uploaded.** Million control, one run, any build type.
In every frame of the profile window, position bytes uploaded are zero and
cull-bound slots rebuilt are zero. Mostly-static million: position bytes
uploaded per frame are at most 16 times the moving cells' voxels
(160,000) and above zero.
Reason for the replacement: settling takes a round, and the light occlusion
grid refreshes stale world boxes for about 62 frames on this control
(read from #4992's plan correction: 3,907 chunks at 64 per frame), so
"after the first frame" is the wrong boundary. The fixture's
warm-up covers both, and its start assert proves it.

**Gate 4, motion control.** Unit tests in `test/ecs/`, any build type. An
entity and an always-dynamic twin are compared after every step of: created
static; settled; thawed; moved; frozen; settled; resettled after a raw
write; saved and loaded. At each step the world transform and every pool
global position are bit-equal to the twin's. The same sequence is run for a
static child of a static parent and for a dynamic child of a static parent.
A further test freezes at a given update and asserts promotion after
exactly one complete round. `render-verify`: the static-cycle rows equal
the `--no-static` rows at maximum delta 0.

Gate 4 also proves the cutover, with unit tests that run the pipelines one
system at a time:

- **Mid-round input change.** For each deriving system in pipeline order, a
  settling entity is mutated right after that system's dispatch and before
  the drain. The test does this once per route: a raw `C_LocalTransform`
  write followed by `resettle`, a `C_VoxelSetNew` edit, `setLodBand`, and a
  fog setter. Each time, the entity is not promoted at that drain, its stamp
  equals `roundsOpened_ + 1` at the drain, it is promoted at the first drain
  after the next complete round, and its world transform, pool positions,
  `lodCulled_` and fog bits are then bit-equal to the twin's.
- **Resettle on a settling entity.** `resettle` called between two deriving
  dispatches restarts the stamp, so promotion moves one round later.
- **Global push in the window.** `pushGlobal` after `MODIFIER_RESOLVE_GLOBAL`
  has dispatched, and again after the round has closed but before the
  drain. In both cases nothing is promoted at that drain. The entity's
  `C_ResolvedFields` and world transform at promotion include the global
  and equal the twin's. A global expiry in `GLOBAL_MODIFIER_DECAY` behaves
  the same way.
- **Global churn.** A one-update global pushed every update holds every
  settling entity. `settled` does not grow and `held_by_global` counts the
  rounds. When the pushes stop, promotion follows after one complete round.
- **Hold.** A staged set is held until seeded, and promotion follows the
  first round after the seed.
- **Global push between the round's close and the drain, ordered.** The
  push lands after the last deriving group has joined, so the entity's own
  stamp is satisfied at that drain (`roundsCompleted_ >= settleStamp_`) and
  only the floor can hold it. The test asserts that it is not promoted at
  that drain, which fails if the drain collects candidates before it reads
  the revision.
- **Grouped deriving systems.** Two non-conflicting deriving systems share
  one `registerPipelineGroups` group, with a worker pool and again with the
  serial fallback. The round does not close until both have dispatched under
  the cursor. With the two at cadences 1 and 2, it closes only after the
  cadence-2 member has dispatched. A mutation after the group joins and
  before the drain restarts the stamp exactly as for a singleton group. An
  assert build checks that the round state is written only on the main
  thread.
- **Cross-pipeline round.** `PROPAGATE_TRANSFORM` in UPDATE and
  `UPDATE_VOXEL_SET_CHILDREN` in RENDER, driven through `runMainLoopFrame`
  with three UPDATE ticks per frame. An entity frozen before the round opens
  is not promoted at any INPUT or UPDATE drain of that round, and
  `roundsCompleted_` is unchanged across all of them. It is promoted first at
  the RENDER drain after `UPDATE_VOXEL_SET_CHILDREN` has dispatched, and its
  pool positions then equal the twin's. The test fails if any drain before
  that RENDER dispatch promotes. A second run has a frame with no UPDATE
  tick in the middle of the round, and the round closes in the next frame
  that runs UPDATE and RENDER.
- **Membership change.** `appendToPipeline` of a new deriving system in the
  middle of a round abandons it, and promotion follows the next complete
  round, which includes the new system.
- **Counter width.** A test-only setter starts the round counters just below
  2^32. Freeze, settle, resettle and settle again run across that boundary,
  with a global push in between. The stamps, the floor and the counters read
  back above 2^32 and promotion happens on the same rounds as in an unseeded
  run, which fails if any of them is stored narrower than 64 bits. The
  header `static_assert`s the field types.
- **Mutation right after promotion.** A `resettle` in the first group after
  the promoting drain leaves the entity settling from the next flush. A
  `C_VoxelSetNew` edit made at the same point leaves it settling from that
  pipeline's drain. In both cases the entity is promoted again after the next
  complete round, equal to the twin.

**Gate 5, forbidden writes.** Unit tests, any build type, one per path:
a registered mover never visits a static entity and its `Walked` count
shows it; `IRPrefab::Modifier::push` on a settled entity thaws it and the
offset reaches the world transform in the same update as on a twin; a raw
`C_LocalTransform` write on a settled entity leaves the world transform
and pool positions unchanged, `resettle` then makes them equal to the
twin's, and in an assert build the audit fires within 256 updates when
`resettle` is not called. One test per row of
[§ Engine mutators](#engine-mutators) and one per fog and LOD trigger.

Gate 5 also proves that the mutators reach their entity and that the policy
holds on every lane:

- **Entity-aware voxel and LOD mutation.** Every `C_VoxelSetNew` mutator in
  the table is called on a settled entity, from C++ and from Lua, including
  the Lua `lodMin` / `lodMax` properties. Each resettles the entity by the
  next drain, and after the next round its state equals the twin's. A C++
  assignment to `lodMin_` leaves `lodCulled_` unchanged, and in an assert
  build the audit fires within 256 updates.
- **Owner adoption.** An entity created static with an empty set, and one
  loaded from a snapshot, have `ownerEntityId_` equal to their own id after
  the first drain. A set edit made before that drain, between two deriving
  dispatches, still ends equal to the twin's.
- **Archetype moves.** Attaching and then detaching `C_LodTierOverride` on
  a settled entity leaves it settling each time, and `lodCulled_` then
  equals the twin's. Attaching a skeleton makes it dynamic (`refused`).
- **Every lane.** For each lane in [§ The matching policy](#the-matching-policy),
  that is `IRSystem::createSystem`, `registerSystem<N>`, a CODEGEN fixture system,
  a Lua EVAL system with and without `reads`, a direct `createSystemDynamic`,
  `executeQuery<Cs...>` with a const and a non-const pack, and
  `executeQueryDynamic`:
  - a writer of `C_LocalTransform` never visits a settling or settled
    entity;
  - a const or `reads` reader visits both, and `setAt` on a `reads` column
    raises;
  - a writer of `C_WorldTransform` that is not a deriving system visits
    neither;
  - one with `Exclude<C_Settled>` visits settling rows but not settled ones,
    and counts in the round.
- **Registration errors and order.** Registering a deriving system that
  writes `C_LocalTransform` asserts. A system created before any entity
  exists and an identical Lua EVAL system created after static entities
  exist match the same nodes. `addSystemExcludeTag<C_Settled>` after
  creation turns a `C_WorldTransform` writer into a deriving system.

**Gate 6, no cost to dynamic entities.** Million control without
`--wave-freeze`, Release, same rounds as gate 2: the per-call means of the
four systems are not above the base arm's by more than twice its round
range. The change note on dynamic entities is bounded the same way: with
`IRPerfGrid --no-static --recolor-every-update` (a flag #5009 adds, which
calls `changeVoxelColorAll` on every cell each update), frame time is not
above base by more than twice its round range.

**Gate 7, render regression.** `python3 scripts/render-verify.py` passes
for `IRPerfGrid` (every row at maximum delta 0), `IRFogDemo`, `IRShapeDebug`
and `IRCanvasStress`; the nine-yaw `IRCanvasStress` set is RGB-identical
between `origin/master` and the head; `python3 scripts/light-verify.py
--if-changed` passes.

**Gate 8, transition cost (new).** Mostly-static million, Release, with the
default `promotionsPerRound_`: `promotion_ms_max` is at most 4 ms on the
reference host. The measured basis is 8,192 moves at 0.28 µs, or 2.3 ms.

**Gate 9, fog still follows the fog (new).** `IRFogDemo`, or a unit test
with a fog component, any build type: a settled subject inside a vision
circle is shown; the circle moves away and the subject is hidden or
ghost-held within `staggerPeriod_` updates; the circle returns and it is
shown again, with mask and carrier bits equal to a dynamic twin's at each
step.

## Evidence

| Reading | Kind | Conditions |
|---|---|---|
| 2.9 to 3.0 fixed updates per rendered frame; `UpdateVoxelSetChildren` 1.300 to 1.368 ms per RENDER call; all UPDATE rows together 2.36 ms per call (arm mean); frame mean 50.0 ms | Measured, recorded in PR #5046, not committed under `docs/perf/` | Million control command above; Release, Metal, Apple M4 Max; head of PR #5046, whose perf demo is master `369cb6b06`'s; four interleaved rounds, all `RESULT=CLEAN` / `QUIET=guarded`; 2026-11-11; host load 3.7 to 6.8 |
| `UpdateVoxelSetChildren` 1.29 ms, `FogRevealEval` 1.21 ms, `PropagateTransform` 0.83 ms, `ModifierDecay` 0.36 ms per update | Measured, not committed | Same command; Release, Metal, Apple M4 Max; master `480f39294`; 2026-11-09; host load 3.7 to 7.1 |
| The four systems take about 8.2 ms per rendered frame today | Derived from the first row: 2.36 × 2.93 + 1.33. An upper bound, because the UPDATE sum covers every UPDATE row | — |
| Per-node dispatch about 0.02 ms per call over 51 archetypes; a per-row early return 0.13 ms per million rows | Measured, recorded in PR #5055 | Million yaw sweep; Release, Metal, Apple M4 Max; 2026-11-12 |
| Archetype move 0.20 to 0.28 µs per entity to add a tag, 0.06 to 0.07 µs to remove one; component sizes | Measured, committed | [`structural-move-probe.txt`](../perf/static-resident-design/structural-move-probe.txt): Release, Apple M4 Max, headless, master `369cb6b06`, 2026-11-12, three runs, load 2.3 to 2.8 |
| The four systems fall to about 0.2 ms per rendered frame on a settled population | Estimated: four dispatches at the measured per-node floor, three of them about three times per frame | Not measured. Gate 2 measures it. |
| 16 MB of positions uploaded per rendered frame today | Read from source: 1,000,000 voxels × 16 bytes, queued by every dispatch of `UPDATE_VOXEL_SET_CHILDREN` (`appendPendingRange` in `tick`, `queuePositionRange` in `endTick`) | Its time is inside `SingleVoxelToCanvasFirst` and has not been split out |
| Memory bandwidth bounds the four systems | Inferred by the epic; no hardware counter | — |

What this buys on the control, by the epic's frame model
`frame = R / (1 - u / 16.67)`: with `u` near zero and the RENDER row gone,
the 50.0 ms frame falls by roughly 8 ms before the position upload and
bound rebuild are counted (estimated). The objective's one update per
rendered frame also needs the render cost `R` under 16.67 ms, which is the
sibling epics' work.

## Alternatives rejected

- **Residents that are not entities.** Baking a static entity into
  chunk-owned voxels would cut it from about 500 bytes to 68. It also
  removes its id, and with it picking, fog BODY verdicts, Lua access,
  snapshots and thaw. That representation already exists for voxels that
  never needed identity; converting between the two is a separate, lossy
  operation and a separate decision.
- **Dropping `C_LocalTransform` or `C_Modifiers` from static entities.**
  It would make the raw write impossible and save 44 or 72 bytes. It breaks
  the contract that every entity has a local transform, which readers all
  over the tree rely on, and it needs an inverse-parent reconstruction on
  thaw that is not bit-exact.
- **One tag.** Covered under [§ The mechanism](#the-mechanism): either the
  last write before the freeze is never derived, or the entity is held
  dynamic with writers still running.
- **A settle hook per deriving system**, run by a queue at the transition.
  It is a second implementation of each system's row logic, and every new
  deriving system would have to remember to add one. Running the systems
  themselves over a settling archetype needs neither.
- **A per-row static flag or a transform compare.** Banned by the rules,
  and measured: a per-row early return still costs 0.13 ms per million rows.
- **A scheduler-level conditional skip.** Not needed. It would save at most
  the per-node dispatch of the gated systems, and it would add a second
  dispatch gate beside cadence with four contract questions to answer.
- **Region-scoped fog re-evaluation.** Indexing settled subjects by fog
  region would bound the gated pass by the area that changed instead of the
  population. It needs an index, a per-region change set and a test per
  trigger. The gated pass in this document is never worse than today, so
  the index is left to a follow-up that measures a fixture with moving
  vision sources first.
- **Re-deriving settled entities when a global modifier changes.** It
  would re-run every deriving system over the whole static population. The
  documented limit is cheaper and a thaw is available.

## Decisions the owner signs

1. Static is declared per entity with `C_Static`; the default is dynamic.
2. Three states by two tags; `C_Settled` is engine-owned. Both tags live in
   the system layer.
3. Freeze, thaw and resettle are deferred to the next structural flush.
   Promotion needs a complete round that opened after the entity's inputs
   last changed: the settle stamp, restarted by every change while settling,
   and the global floor, restarted by every global-modifier change. A round
   spans INPUT, UPDATE and RENDER, and only the join of its last deriving
   group closes it. Promotion is capped per round (default 8,192). Any other archetype move returns a
   settled entity to settling.
4. A static entity needs a static parent; thaw and resettle cascade
   downward.
5. The first implementation is limited to GRID, CPU-direct, non-skeletal
   entities.
6. System writes to pose inputs are impossible; mutator writes thaw or
   resettle; raw writes are retained, audited in assert builds, and applied
   by `resettle`. Voxel-set mutators reach their entity through
   `ownerEntityId_`, with no signature change.
7. The matching policy: a guard trait on each component, recorded per
   `ComponentId` by the entity layer, and one exclusion function on every
   registration and query lane. A deriving system is one that excludes
   `C_Settled` or carries `GatesSettled`.
8. Fog and LOD stay correct through gated passes with the trigger tables
   above; fog cost is unchanged while the fog changes every update.
9. Modifiers on a static entity are frozen, and later global modifiers do
   not reach it.
10. The four rule changes.
11. The public contract changes, including the Lua rule that a listed
    component counts as written unless it is under `reads`.
12. The nine gates and four fixtures.
