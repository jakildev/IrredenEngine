---
paths:
  - "engine/**/*.{hpp,cpp,h,cc}"
  - "creations/**/*.{hpp,cpp,h,cc}"
---

# ECS rules: getComponent in ticks, deferred entity ops, component method tiers

## The ECS footgun: never call getComponent inside a per-entity tick

> **Never** call `IREntity::getComponent` or `IREntity::getComponentOptional` on a system's *own* iterating entity inside its per-entity tick function.

Each call is a hash-map lookup, a linear scan of the archetype, and another hash-map lookup. At scale, it dominates the frame.

The fix is mechanical: **add the component to the system's template parameters**. The data is then accessed via contiguous archetype-column iteration — array index, not random lookup.

Alternatives, in order of preference:

1. Include the component in `createSystem<...>` template params.
2. Cache the data in an existing component at creation time (e.g. store the canvas entity ID in `C_VoxelSetNew` during allocation rather than looking it up every frame).
3. Use `beginTick` / `endTick` for once-per-frame lookups.
4. Use `relationTick` for per-parent-group lookups — fires once per unique parent entity when using `CHILD_OF` or other relation queries.

## Foreign-entity lookups: contact pairs, messages, target entities

The "no getComponent in tick" rule is about entities the system iterates — adding to template params is the fix. For **dynamically-determined foreign entities** (the *other* entity in a contact pair, a target entity stored in a component, a parent looked up at runtime), getComponent is sometimes the only option, but the **established pattern is to batch the foreign entities as a vector** rather than calling `getComponent` per-pair inside the tick.

The right shape for collision / message / event systems:

- The event component (e.g. `C_ContactEvent`) carries a vector of all involved entities or a vector of contact pairs, batched by the producing system at frame boundary.
- The consumer system iterates the **vector**, not individual events. The relevant component data is either pre-fetched into the event itself by the producer, or looked up once per batch in `beginTick`/`endTick`, not per-entity.

This pattern is **not yet uniformly applied** in the codebase. Known violation:

- `engine/prefabs/irreden/update/systems/system_spring_platform.hpp:54-55` — calls `getComponentOptional<C_Velocity3D>(contact.otherEntity_)` per contact inside the tick.

When refactoring collision/message systems, drive toward the batched-vector pattern. New collision systems must use it from the start.

## Deferred entity operations during tick

`createEntity`, `setComponent`, `removeComponent`, and `removeEntity` called mid-iteration in a per-entity tick **silently invalidate component addresses** when they trigger an archetype change. The system that's iterating may skip or revisit entities, and any captured component reference becomes wild.

Use the deferred variants (`IREntity::deferredCreate`, `deferredSetComponent`, etc.) and let `flushStructuralChanges` run at the end of the pipeline.

## Component method tiers

See [`engine/prefabs/CLAUDE.md` § "Component method rules"](../engine/prefabs/CLAUDE.md#component-method-rules) — that file is the canonical home per `CLAUDE-BASELINE.md` §74–79.

## No dirty flags on components

Rule:

> **Never** add a `bool dirty_` (or `bool needsUpload_`, `bool changed_`, etc.) field to a component to gate a per-frame CPU→GPU sync step. Choose one of the two honest patterns instead.

A dirty flag papers over a question the design needs to answer: *who owns the data between mutations?* Either:

1. **Push at mutation time.** Each mutating method (`setCell`, `clear`, ...) writes the affected range directly to the destination resource — `Buffer::subData` for a slice, `Texture2D::subImage2D` for a region. The destination is always current; the system never gates an upload. When the per-mutation upload cost is high enough to dominate the workload — Metal `subData`'s buffer orphan is the canonical case — wrap the per-write pattern in a per-frame **pending-list flush**: the mutator queues an index into a vector, and the owning system flushes once per frame as coalesced contiguous-run `subData` calls. The flush is bounded, deterministic, and never re-uploads "untouched" slots (so GPU-authored state is preserved between mutations). Canonical example: `C_GPUParticlePool::writeSlot` appends to `pendingIndices_`; `UPDATE_GPU_PARTICLES` calls `pool.flushPendingSpawns()` immediately before its compute dispatch reads the SSBO.

2. **GPU owns ongoing state.** If the GPU mutates the same resource every frame (compute-shader simulation, GPU-side accumulation), the CPU mirror is a one-shot seed only. Initialize the GPU resource once in the component ctor (`subData` immediately after `createResource`) and never read from the CPU mirror again — its values are stale by frame 1 anyway. The CPU vector remains useful for allocator-side bookkeeping (e.g. dead-slot scans) but is no longer a source of truth.

Why the dirty-flag pattern is wrong:

- **Hides the ownership contract.** A `dirty_` field reads as "CPU has the truth; GPU mirrors it." When the GPU also mutates the buffer (Phase 1 particle update), re-uploading on dirty clobbers the GPU's per-frame writes. The bug is invisible until a continuous-emitter use case lands.
- **Defers the correct fix.** Per-write `subData` is `O(bytes changed)` on OpenGL (true partial patch); on Metal it orphans the whole buffer per call (`O(buffer size)` regardless of patch size, for synchronization). A dirty-gated full re-upload is `O(buffer size)` per dirty frame on both backends. The "optimization" is usually a pessimization unless mutations are dense across the buffer in a single frame — at which point the right shape is a sparse dirty-range tracker (start/end indices), not a single boolean. On Metal specifically, high-rate per-frame mutators should also batch CPU-side writes into a single per-frame `subData` to amortize the orphan churn.
- **Encourages sync drift.** "Set dirty when X" sites accumulate over time; eventually a mutator forgets the flag and writes silently fail to propagate. Per-write upload is its own audit.

Allowed exception: the destination resource is **strictly CPU-authored, GPU-read-only, and re-uploading the whole buffer is genuinely expensive enough** that the optimization pays off. The fog-of-war texture is the only current example — `C_CanvasFogOfWar` uploads a 256×256 RGBA8 texture (256 KiB), the GPU never writes back, and per-cell `subImage2D` would split `revealRadius`'s loop into ~hundreds of API calls. Document the exception in the component header and in the `## Live deviations` block below; new components should not introduce dirty flags.

### Live deviations

- `engine/prefabs/irreden/render/components/component_canvas_fog_of_war.hpp` — `C_CanvasFogOfWar::dirty_` and `allUnexplored_` gate the per-frame `subImage2D` upload of the 256² fog texture. Documented exception (CPU-authored, GPU-read-only, full-texture upload). T-161 evaluated migration to per-region `subImage2D` and deferred; see [`docs/design/fog-of-war-upload-strategy.md`](../../docs/design/fog-of-war-upload-strategy.md) for the analysis, the trigger conditions for revisiting, and the mechanical Strategy C migration sketch.

## Allocations in hot tick paths

Allocating memory inside a per-entity tick (`new`, `std::vector::push_back`, `std::string` concatenation, `std::map::operator[]` insertion, `std::make_unique`) is a frame-time landmine. Reserve once at `beginTick`; reuse capacity across frames; clear without releasing.

If the data structure size depends on input that varies per frame, the allocation belongs in `beginTick` (with a high-water-mark `reserve`) or in `SystemParams` (where it persists between frames and only grows).

## Manager accessor calls inside ticks

Calling `IREntity::*`, `IRRender::*`, `IRAudio::*` accessors **inline within a tick** is fine. The rule against "holding manager pointers across frames outside World's lifetime" is about *storing* a manager pointer or reference somewhere that outlives `World`. Inline calls into the manager that finish before the tick returns are not affected.

## Naming

| Context           | Convention                                         |
|-------------------|----------------------------------------------------|
| Private members   | `m_` prefix                                        |
| Public members    | trailing `_`                                       |
| Components        | `C_` prefix                                        |
| Enum values       | `SCREAMING_SNAKE_CASE`                             |
| Compute shaders   | `c_` prefix                                        |
| Vertex shaders    | `v_` prefix                                        |
| Fragment shaders  | `f_` prefix                                        |
| Geometry shaders  | `g_` prefix                                        |
| Header helpers    | nested `detail` namespace (not anonymous, not feature-named) |

Prefer descriptive names over abbreviations (`viewCenterIso` not `vcIso`). Use a lowercase `detail` namespace for header-only helpers under the owning namespace (`IRSystem::detail`, `IRRender::detail`). Don't use anonymous namespaces in headers; keep them in `.cpp`.
