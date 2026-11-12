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
  every system that declares a write to them stops matching static
  archetypes. The write is impossible, not forbidden by convention.
- The existing systems then derive from those frozen inputs one more time,
  in pipeline order. When that round is complete the engine adds a second
  tag, `C_Settled`, and the deriving systems stop matching the entity. No
  second code path computes derived state: a settled entity holds exactly
  what the ordinary systems last wrote.
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
| Round | One dispatch of every registered deriving system, completed in pipeline order. |

## The mechanism

Two empty tag components in `engine/prefabs/irreden/common/components/`
give an entity one of three states. The state is archetype membership and
nothing else: there is no flag on any component and no value compared
between updates.

| State | Tags | Pose writers match it | Deriving systems match it |
|---|---|---|---|
| Dynamic | none | yes | yes |
| Settling | `C_Static` | no | yes |
| Settled | `C_Static`, `C_Settled` | no | no, or gated |

`C_Static` is the owner's declaration. `C_Settled` is engine-owned: only the
engine adds or removes it, and it never exists without `C_Static`.

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

The engine adds `C_Settled`. The owner does not call anything.

A settling entity is promoted at the first structural flush after a round
that began after the entity entered the settling state. A round is one
completed dispatch of every deriving system that is a member of a registered
pipeline, in pipeline order (INPUT, then UPDATE, then RENDER, in declaration
order within each). An off-cadence dispatch does not count. With every
deriving system at cadence 1 a round is one frame that runs at least one
fixed update, so promotion follows freeze by one to two frames.

The round is defined over whatever the creation registered, so the rule
holds for `UPDATE_VOXEL_SET_CHILDREN` in UPDATE or in RENDER without naming
either placement.

A deriving system that could not finish for a row holds it for that round.
The holds are:

- `SEED_STAGED_VOXELS`: the set is still staged (no canvas yet).
- The fog evaluators: the set is ghost-held at a pose other than its own.
  This happens only when an entity moved while hidden and was then frozen.
  Once a static entity has been seen at its pose, its ghost pose equals its
  pose and it is not held.

A held entity stays settling and is walked like a dynamic one. It is
counted in the report's `held` witness.

**Budget.** Promotions per round are capped by
`C_StaticSettings::promotionsPerRound_`, a singleton component. The default
is 8,192, which is about 2.3 ms at the measured cost. Zero means no cap; a
loader sets it for the duration of a load and accepts one long frame.
Entities over the cap wait for the next round and are promoted oldest
first.

### Thaw: static to dynamic

`IRPrefab::Static::thaw(entity)` removes both tags at the next flush. From
that flush every system matches the entity again. One archetype move,
uncapped: a caller that thaws is about to change the entity and must not
wait. Thawing an entity that is already dynamic is a no-op.

Thaw cascades to every static descendant, because a descendant's world
transform depends on the entity's.

### Resettle: settled to settling

`IRPrefab::Static::resettle(entity)` removes `C_Settled` and leaves
`C_Static`. The entity is derived again in the next round and promoted. It
is the operation behind every engine mutator that changes a static entity's
inputs once, and it is the sanctioned follow-up to a raw write (see
[§ Forbidden writes](#forbidden-writes)). It cascades to static descendants
for the same reason thaw does.

### Engine mutators

A mutator that changes something a static entity's derived state depends on
does the transition itself. The caller never has to remember it
(`.claude/rules/cpp-ecs.md` § "System-owned invariants: encapsulate, don't
delegate to callers").

| Mutator | Effect on a static target |
|---|---|
| `IRPrefab::Modifier::push`, `upsertBySource` | Thaw. A modifier needs resolving and decaying every update. |
| `IRPrefab::Modifier::removeBySource`, including the pre-destroy sweep | Resettle. The input changed once. |
| `IREntity::setParent`, `clearParent` | Thaw. |
| Destroying a static entity's parent | Thaw the children, from a pre-destroy hook. |
| `IRPrefab::RotationMode::setMode` | Thaw. |
| Attaching a gizmo to the entity as its drag anchor | Thaw. |
| Allocating a GPU transform slot or attaching a skeleton | Thaw. |
| `C_VoxelSetNew` mutators that change records, alpha, span or canvas (`editVoxels`, `carve`, the bulk mutators, `resyncAfterRawEdits`, `resyncAfterAuthoredEdits`, `attachToCanvas`, the canvas-teardown re-stage) | Resettle. |
| `IRPrefab::Fog::setHiddenPolicy`, `setSubjectClass`, `setEntityRevealGoverned`, and the new `setOverride`, `setChannels` | Resettle. |
| The `lodMin` / `lodMax` setters and attaching or changing `C_LodTierOverride` | Resettle. |

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
system. A per-entity `C_LodTierOverride` or band change resettles the
entity.

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
| A system tick parameter, C++ or Lua | `VELOCITY_3D` over an entity with `C_Velocity3D` | **Impossible.** A system that declares a write to a pose input does not match static archetypes, so the row is never visited. The entity does not move, and nothing is written. |
| An engine mutator | `IRPrefab::Modifier::push(entity, …)` | **Honoured.** The mutator thaws or resettles the entity as tabled above, and the write takes effect with the latency it has on a dynamic entity. |
| A raw reference | `IREntity::getComponent<C_LocalTransform>(entity).translation_ = p`, a Lua entity-handle field write, `forEachComponent` | **Retained, not derived.** The value is stored. Derived state does not change until the entity is next resettled or thawed, at which point it is derived from the then-current inputs. |

For the raw path:

- The sanctioned form is the raw write followed by
  `IRPrefab::Static::resettle(entity)` (once, after all writes), or `thaw`
  before sustained motion. This mirrors `resyncAfterRawEdits()` on
  `C_VoxelSetNew`.
- An assert build audits it. Each fixed update the audit recomposes a slice
  of the settled rows from their inputs and compares the result with the
  stored `C_WorldTransform` and the pool global positions; every settled
  row is covered at least once per 256 fixed updates. A mismatch is an
  `IR_ASSERT` naming the entity. The audit only asserts. It never skips or
  triggers work, so it is a validator and not a gate.
- A raw write to a subject's fog inputs (`override_`, `channels_`) on a
  settled entity is read by the next armed fog pass. It is delayed, not
  lost; the setters avoid the delay.
- A raw write to `C_WorldTransform` on a settled entity is the same case as
  a raw write to `C_LocalTransform`. On a dynamic entity it survives until
  the next `PROPAGATE_TRANSFORM`, as today.

**Which systems are pose writers.** The rule is applied at registration,
from the access each system already declares:

- A system that declares a write to a pose input (`C_LocalTransform`,
  `C_Modifiers`) never matches an archetype that contains `C_Static`.
- A system that declares a write to derived pose (`C_WorldTransform`,
  `C_ResolvedFields`) never matches one either, unless it is a deriving
  system.
- `executeQuery` applies the same rule.
- A Lua system declares no access today, so every component it lists counts
  as written. A new optional `reads = { … }` key lists the ones it only
  reads; `setAt` on such a column raises.

Twelve engine systems take `C_LocalTransform` by non-const reference and
four take `C_WorldTransform` that way (read from source, `369cb6b06`). Some
only read it, for example `system_contact_note_burst.hpp` and
`system_periodic_idle_note_burst.hpp`. #5009 const-corrects those so they
keep matching static entities; its PR lists each one.

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

1. **Implicit exclusion at registration**, as described under
   [§ Forbidden writes](#forbidden-writes).
2. **Round tracking.** It knows which registered systems are deriving
   systems and notes each completed dispatch. This is a few integers on the
   system manager.
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
| `C_Static`, `C_Settled` columns | 0 | 2 | Read from source: a column is a `std::vector` of the component, and an empty struct occupies one byte |
| **Resident in memory** | **about 498** | **about 500** | Sum |
| **Read by the GPU** | 36 | 36 | Read from source: position 16, record 12, id 8 |
| **Streamed per fixed update** | about 760 | 0 | Before: the ticket's derived estimate for the four systems. After: no deriving system or pose writer matches the row. |
| **Position bytes uploaded per rendered frame** | 16 | 0 | Read from source: one `VoxelGpuPosition` per voxel, re-queued by every `UPDATE_VOXEL_SET_CHILDREN` dispatch today |

While a gated fog pass is armed it streams the evaluator's own read set for
that update, as today. Promotion bookkeeping is transient and estimated at
16 bytes per settling entity.

Memory per resident does not fall, and this design does not try to make it
fall. The two levers that would are named in
[§ Alternatives rejected](#alternatives-rejected).

## Public contract changes

Listed for the owner's approval. Every one is additive: a creation that
attaches no `C_Static` sees no change in behaviour.

| Change | Kind |
|---|---|
| `C_Static`, `C_Settled`, `C_StaticSettings` in `engine/prefabs/irreden/common/components/` | New components. `C_Static` is saved; the other two are not. |
| `IRPrefab::Static::freeze`, `thaw`, `resettle`, `isStatic`, `isSettled`, `stats` | New prefab namespace. |
| `IRStatic` Lua table with the same functions; `reads = { … }` on Lua `registerSystem` | New Lua surface. |
| `IRSystem::GatesSettled` access tag; `IRSystem::reportRowsWalked` | New scheduler surface. |
| `IRPrefab::Fog::setOverride`, `setChannels` | New setters. The fields stay public. |
| Systems that write pose inputs do not match static archetypes | New matching rule. It affects only entities that carry `C_Static`. |
| The mutators in [§ Engine mutators](#engine-mutators) thaw or resettle a static target | New behaviour for static targets only. |
| Global modifiers pushed after an entity settles do not reach it | New, documented limit. A creation that needs one to apply thaws the entity. An assert build logs once per field when a global modifier is pushed while settled entities exist. |
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

All three are `IRPerfGrid` modes; #5009 adds the flags.

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

Each fixture asserts, when its profile window or its first capture begins,
that no entity is settling and that its script has finished. The profile
report prints the `Static residents` witnesses: `static`, `settling`,
`settled`, `promoted`, `thawed`, `resettled`, `held`, `refused`,
`promotion_ms_max`, position bytes uploaded per frame, and cull-bound slots
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

**Gate 5, forbidden writes.** Unit tests, any build type, one per path:
a registered mover never visits a static entity and its `Walked` count
shows it; `IRPrefab::Modifier::push` on a settled entity thaws it and the
offset reaches the world transform in the same update as on a twin; a raw
`C_LocalTransform` write on a settled entity leaves the world transform
and pool positions unchanged, `resettle` then makes them equal to the
twin's, and in an assert build the audit fires within 256 updates when
`resettle` is not called. One test per row of
[§ Engine mutators](#engine-mutators) and one per fog and LOD trigger.

**Gate 6, no cost to dynamic entities.** Million control without
`--wave-freeze`, Release, same rounds as gate 2: the per-call means of the
four systems are not above the base arm's by more than twice its round
range.

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
2. Three states by two tags; `C_Settled` is engine-owned.
3. Freeze, thaw and resettle are deferred to the next structural flush;
   promotion follows one complete round and is capped per round (default
   8,192).
4. A static entity needs a static parent; thaw and resettle cascade
   downward.
5. The first implementation is limited to GRID, CPU-direct, non-skeletal
   entities.
6. System writes to pose inputs are impossible; mutator writes thaw or
   resettle; raw writes are retained, audited in assert builds, and applied
   by `resettle`.
7. Fog and LOD stay correct through gated passes with the trigger tables
   above; fog cost is unchanged while the fog changes every update.
8. Modifiers on a static entity are frozen, and later global modifiers do
   not reach it.
9. The four rule changes.
10. The public contract changes, including the Lua rule that a listed
    component counts as written unless it is under `reads`.
11. The nine gates and three fixtures.
