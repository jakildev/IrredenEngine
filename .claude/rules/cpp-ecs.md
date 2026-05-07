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

A component's methods fall into three categories. Anything outside (a) and (b), and not on the documented exceptions list, is a violation.

**(a) Pure data.** Fields and a constructor that initializes them. Most components in `common/`, `input/`, and tag-style components.

**(b) Self-only helpers (allowed).** Methods that read or write only the component's own fields (and stack-locals derived from them). Examples:

- `C_PeriodicIdle::tick()` advances its own angle.
- `C_CanvasFogOfWar::setCell()` writes its own grid.
- `toGPUFormat()` converts own fields into a render struct.

**(c) Cross-entity reaches (forbidden).** Methods that look up *another* entity through a stored `EntityId` field via `IREntity::getComponent`, `setComponent`, `createEntity`, `setParent`, or `getEntity`.

These belong in **a system** (per-frame work) or one of:

- **Entity builder** — `template <> struct IREntity::Prefab<NAME>` with a `create()` that wires up the entity bundle. Example: `engine/prefabs/irreden/render/entities/entity_trixel_canvas.hpp`.
- **Prefab-scoped namespace** — a sibling `<feature>.hpp` exposing a `namespace IRPrefab::Foo` with free functions. The namespace owns the entity-lookup logic; callers don't carry the entity id. Example: `engine/prefabs/irreden/render/fog_of_war.hpp`. Use this when an API needs an ergonomic free-function shape and the caller is unlikely to hold the entity id.

The split keeps component layout trivial (good for archetype iteration) and concentrates per-entity orchestration where it belongs.

### Documented exceptions to (c)

These patterns *look* like (c) but are accepted because the alternatives are worse:

- **GPU / IO resource RAII.** A constructor that calls `IRRender::createResource<>` and a destructor that calls `destroyResource<>` is fine — the component IS the resource owner, and splitting allocation into a system makes the lifetime contract harder to enforce. Examples: `C_TriangleCanvasTextures`, `C_TrixelFramebuffer`, `C_CanvasFogOfWar`, `C_CanvasSunShadow`, `C_CanvasAOTexture`, `C_CanvasLightVolume`.
- **`onDestroy()` IO cleanup.** A component that hooks the entity's destroy event to flush an external side effect (e.g. `C_MidiNote::onDestroy()` sending NOTE_OFF) is fine when the cleanup must be synchronized with entity death and a per-component hook is simpler than a "scan dying entities" system. Document the side effect in the domain `CLAUDE.md`.
- **Constructor snapshots ambient state.** A constructor that reads `IRRender::getActiveCanvasEntity()` (or similar) to bind the component to the currently-active canvas is fine — no other entity is mutated, and the alternative is forcing every caller to thread the canvas id. Examples: `C_VoxelSetNew`, `C_ShapeDescriptor`.

Anything outside (a)/(b) and not on the exceptions list above is a (c) violation and should be moved to a system, builder, or namespace.

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
