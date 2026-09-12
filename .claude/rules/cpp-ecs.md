---
paths:
  - "engine/**/*.{hpp,cpp,h,cc}"
  - "creations/**/*.{hpp,cpp,h,cc}"
---

> Sweep with `fleet-rules-sweep`, never `rg`/`Grep` rooted at `creations/`
> — see [`README.md`](README.md).

# ECS rules: getComponent in ticks, deferred entity ops, component method tiers

## The ECS footgun: never call getComponent inside a per-entity tick

> **Never** call `IREntity::getComponent` or `IREntity::getComponentOptional`
> on a system's *own* iterating entity inside its per-entity tick function.

Each call is two hash-map lookups and an archetype scan; at scale it
dominates the frame. Fixes, in order of preference:

1. Add the component to the system's `createSystem<...>` template params —
   contiguous archetype-column access.
2. Cache the data in an existing component at creation time.
3. `beginTick` / `endTick` for once-per-frame lookups.
4. `relationTick` for per-parent-group lookups (fires once per unique parent
   under `CHILD_OF` or another relation query).

## Foreign-entity lookups: contact pairs, messages, target entities

For dynamically-determined foreign entities (the other half of a contact
pair, a target stored in a component, a runtime parent), batch the foreign
entities as a vector instead of calling `getComponent` per pair in the tick:
the producing system fills the event component (`C_ContactEvent`, …) with the
involved entities or pairs at the frame boundary; the consumer iterates that
vector, with component data either pre-fetched by the producer or looked up
once per batch in `beginTick`/`endTick`. New collision / message systems use
this shape from the start; refactors drive toward it.

## Deferred entity operations during tick

`createEntity`, `setComponent`, `removeComponent`, and `removeEntity` called
mid-iteration silently invalidate component addresses on an archetype change.
Use the deferred variants (`IREntity::deferredCreate`, `deferredSetComponent`,
…) and let `flushStructuralChanges` run at pipeline end.

## Component method tiers

See [`engine/prefabs/CLAUDE.md` § "Component method rules"](../../engine/prefabs/CLAUDE.md#component-method-rules) — the canonical home.

## No dirty flags on components

> **Never** add a `bool dirty_` (`needsUpload_`, `changed_`, …) to a
> component to gate a per-frame CPU→GPU sync. Pick one of two honest
> patterns instead.

1. **Push at mutation time.** Each mutating method writes the affected range
   to the destination resource directly (`Buffer::subData`,
   `Texture2D::subImage2D`). When per-mutation upload cost dominates (Metal
   `subData` orphans the whole buffer), the mutator queues an index into a
   pending list and the owning system flushes once per frame as coalesced
   contiguous-run `subData` calls — bounded, deterministic, never
   re-uploading untouched slots. Example: `C_GPUParticlePool::writeSlot` →
   `pendingIndices_`, flushed by `UPDATE_GPU_PARTICLES` before its compute
   dispatch.
2. **GPU owns ongoing state.** If the GPU mutates the resource every frame,
   the CPU mirror is a one-shot seed: upload once in the component ctor and
   never read the mirror as truth again (allocator bookkeeping is fine).

A dirty flag hides the ownership contract (re-uploading on dirty clobbers the
GPU's per-frame writes), is usually a pessimization (per-write `subData` is
`O(bytes changed)` on GL; a dirty-gated full re-upload is `O(buffer)`), and
accumulates "set dirty when X" sites that drift. Dense whole-buffer mutation
in one frame wants a sparse dirty-range tracker, not a boolean; on Metal,
batch high-rate CPU writes into one per-frame `subData`.

Allowed exception: the resource is strictly CPU-authored, GPU-read-only, and
re-uploading it whole is genuinely expensive. Document it in the component
header and in §"Live deviations" below.

### A snapshot-compare-and-early-return is a dirty flag in disguise

Caching last frame's inputs on the component (`lastFoo_`,
`cachedTransform_`, `hasLastTransform_`) and early-returning when they match
is the same anti-pattern under a different field name — it bloats the
component and accumulates re-stamp sites. Per-frame work on a rendered
entity is unconditional; the only honest skip is that the result is not
observable this frame — gate on **visibility / cull**
(`C_VoxelPool::isRangeVisible`), never on input-equality.

### Multi-field honest gates must stay consistent across failure paths

When a state gate is composed of coupled fields (`C_VoxelSetNew`'s staged
gate: `numVoxels_ == 0` **and** `pendingVoxels_` non-empty), every failure /
early-return path leaves them mutually consistent — a shared mutator that
zeroes one on failure zeroes (or preserves) all of them together.

### A change-gated recompute must trigger on every input its function reads

When a per-frame recompute is legitimately gated on a change signal, the
trigger set covers **every** mutable input the recomputed function reads
(positions, transform indices, active masks, …). Enumerate the read set,
give each input a trigger, and test each trigger independently.

## System-owned invariants: encapsulate, don't delegate to callers

> When using a subsystem requires follow-up bookkeeping to keep its derived
> state consistent, that bookkeeping is the subsystem's job — never a manual
> step each creation replicates.

Two routes, chosen by when the derived state changes:

1. **A component mutator that resyncs at mutation time** — when derived
   state changes only on explicit edits. The method applies the edit **and**
   the resync in one call so no call shape can skip the bookkeeping:
   `C_VoxelSetNew::editVoxels(fn)` / `::carve(shouldDeactivate)` restore every
   derived invariant (rotation-source mirror → pool active-mask → face
   occupancy) through one private `resyncDerivedState()`.
   `resyncAfterRawEdits()` is the escape hatch for a multi-pass raw edit;
   new code uses the mutator.
2. **`beginTick`/`endTick` + pipeline ordering** — when derived state tracks
   a live input every frame. The system recomputes unconditionally on the
   pipeline's schedule (`REBUILD_GRID_VOXELS` re-rasterizes a GRID-mode
   entity from its live `C_WorldTransform` while on-screen); no creation
   calls a resync, and the no-dirty-flags rule rules out a cached snapshot
   deciding whether to skip.

## Allocations in hot tick paths

No `new`, `std::vector::push_back` growth, `std::string` concatenation,
`std::map::operator[]` insertion, or `std::make_unique` inside a per-entity
tick. Reserve once in `beginTick` (high-water-mark `reserve`) or in
`SystemParams`; reuse capacity across frames; clear without releasing.

## Manager accessor calls inside ticks

Inline `IREntity::*` / `IRRender::*` / `IRAudio::*` calls that finish before
the tick returns are fine. The ban is on *storing* a manager pointer or
reference somewhere that outlives `World`.

## Naming

Canonical table: [`docs/agents/CLAUDE-BASELINE.md` §"Naming"](../../docs/agents/CLAUDE-BASELINE.md#naming).
Header-only helpers go in a nested lowercase `detail` namespace under the
owning namespace (`IRSystem::detail`); anonymous namespaces stay in `.cpp`.
The anonymous-namespace and `*Detail`-namespace bans are executed by the
`header-checks` target and CI — scope and baseline in
[`cpp-globals.md` §"Detection"](cpp-globals.md).

## Live deviations

- `engine/prefabs/irreden/render/components/component_canvas_fog_of_war.hpp`
  — `C_CanvasFogOfWar::dirty_` / `allUnexplored_` gate the per-frame
  `subImage2D` upload of the 256² fog texture (CPU-authored, GPU-read-only,
  whole-texture upload; performed by `VOXEL_TO_TRIXEL_STAGE_1`, read-only in
  `FOG_TO_TRIXEL`). Migration to per-region `subImage2D` was evaluated and
  deferred: [`docs/design/fog-of-war-upload-strategy.md`](../../docs/design/fog-of-war-upload-strategy.md).
- `engine/prefabs/irreden/update/systems/system_spring_platform.hpp` —
  per-contact `getComponentOptional<C_Velocity3D>(contact.otherEntity_)`
  inside the tick; migrate to the batched-vector pattern when touching it.
