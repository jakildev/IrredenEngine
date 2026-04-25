# engine/prefabs/irreden/common/ — position, identity, tags

Foundation layer every other domain builds on. Position, transform, name,
selection state, and the player tag. No simulation systems — those live
in `update/`.

## Key components

- `C_Position3D` — local vec3.
- `C_PositionGlobal3D` — world-space vec3. **Auto-added by `createEntity(...)`**.
- `C_PositionOffset3D` — ephemeral delta added on top of global.
  **Auto-added by `createEntity(...)`**.
- `C_Rotation` — Euler vec3.
- `C_Name` — debug/display string.
- `C_Player` — tag for player-controlled entities.
- `C_Selected` — tag for UI selection.
- `C_Modifiers` / `C_GlobalModifiers` / `C_NoGlobalModifiers` /
  `C_LambdaModifiers` / `C_ResolvedFields` — generic modifier
  framework. See [Modifier framework](#modifier-framework) below.

## Systems

None. Position math is read-only in `common/` — write paths live in
`update/systems/` (velocity, physics, animation).

## Modifier framework

`component_modifiers.hpp` declares the generic
`base + N modifications → effective` framework that generalizes the
position-offset / velocity-drag pattern. It is an engine-level
mechanism: any feature that wants a stack of additive / multiplicative
/ clamp / override modulations on a scalar field can register a
`FieldBindingId` and push `Modifier` records onto the entity's
`C_Modifiers` vector. The resolver pipeline composes them once per
UPDATE tick and writes the result to `C_ResolvedFields`.

The framework ships in two phases: type declarations (component types,
`Modifier` struct, static asserts) and the runtime
(`IRPrefab::Modifier::` free-function API, `FieldBindingId` registry,
the resolver systems, `applyToField`) shipped on top.

Runtime entry points (all `inline`, header-only):

- `registerField(name)` / `fieldName(id)` / `fieldCount()` — dense
  registry, init-time only.
- `push(target, field, kind, param, source, ticks)` — push one
  structured modifier onto an entity. `pushGlobal(...)` targets the
  singleton; `pushLambda(...)` writes the escape-hatch component.
  All three reject `kInvalidFieldId` defensively.
- `removeBySource(source)` — sweeps every `C_Modifiers`,
  `C_GlobalModifiers`, and `C_LambdaModifiers` in the world,
  dropping entries whose `source_` matches. **Manual-only in v1**;
  see "Auto-sweep on entity destruction" below.
- `applyToField(target, field, base) → float` — direct query. Shares
  one evaluator with the resolver pipeline so the cache and direct
  paths give the same answer for the same input.
- `registerResolverPipeline()` — call once at creation init. Creates
  the singleton globals entity (named `"modifierGlobals"`) and
  registers the four resolver systems in canonical order. Returns
  the four `SystemId`s in pipeline order so the caller splices them
  into its `IRTime::UPDATE` pipeline.

Composition core lives in `modifier_compose.hpp` and is called from
both the resolver tick and `applyToField`. Order is non-obvious:

1. Latest `OVERRIDE` in (`globals` ++ `entity_mods`) wins; an
   `OVERRIDE` in `entity_mods` trumps one in `globals`. Everything
   earlier than the chosen `OVERRIDE` is discarded.
2. `ADD` / `MULTIPLY` / `SET` apply in push-order across both
   vectors (vector A first, then vector B).
3. `CLAMP_MIN` / `CLAMP_MAX` apply last across the surviving
   modifiers — even if they appear earlier than the algebra in push-
   order. This is the "always after the algebra so they bound the
   result" rule from the design doc.

Full design — locked choices, rationale, audit, public-API surface,
and decomposition — is in `docs/design/modifiers.md`. Read that
before adding to the framework or migrating an existing
`base + offset` pattern onto it.

Key invariants the design rests on:

- `Modifier` stays **trivially-copyable**. Anything needing inline
  `std::function` or `std::string` belongs in `C_LambdaModifiers`,
  not `C_Modifiers`.
- Public API lives in the `IRPrefab::Modifier::` namespace per the
  prefab-layer principle in `engine/prefabs/irreden/render/CLAUDE.md`,
  NOT in `IRRender::` or any engine-library-level namespace.
- Globals + exemption are dispatched via **archetype routing**
  (separate include / exclude resolver systems on
  `C_NoGlobalModifiers`), never via per-entity branching inside a
  tick body.
- Decay is built-in only as `ticksRemaining_` (an `int32_t` counter
  with `-1` as the sentinel for "no decay"). Curved / source-driven
  decay is the source entity's job, not the modifier struct's.

### Open follow-ups (runtime gaps)

The current runtime ships four of the five resolver systems plus the
manual-call sweep API. Two design-mandated paths are deferred:

- **`MODIFIER_RESOLVE_EXEMPT` archetype-routed exemption.** The
  design routes entities tagged `C_NoGlobalModifiers` to a sibling
  resolver that skips globals. `engine/system/` does not yet expose
  an exclude-tag filter mechanism — `addSystemTag<T>` is include-
  only. Until an `addSystemExcludeTag<T>` (or equivalent) lands,
  exempt-tagged entities still receive globals. The `SystemName`
  enum slot is reserved.
- **Auto-sweep on entity destruction.** The design contract is for
  `removeModifiersFromSource(entityId)` to fire inside
  `EntityManager::destroyEntity` *before* `returnEntityToPool`, so
  recycled `EntityId`s never inherit a previous owner's modifiers.
  No pre-destroy hook registry exists in `engine/entity/` yet;
  callers must invoke `IRPrefab::Modifier::removeBySource(id)`
  explicitly before destroying a source entity. `EntityId` has no
  generation counter, so deferring the sweep one tick is unsafe —
  the hook is the right shape; it just needs the engine plumbing.

Both gaps need engine-level additions, not prefab-only work. File a
follow-up task before relying on either.

## Commands

- `command_suite_camera.hpp` — registers `ZOOM_IN`, `ZOOM_OUT`,
  `MOVE_CAMERA_*`, `CLOSE_WINDOW`. A convenience bundle creations pick
  up en masse.

## Gotchas

- **`createEntity` always adds `C_PositionGlobal3D` + `C_PositionOffset3D`.**
  Rendered position is `global + offset`. Never infer a draw position
  from `C_Position3D` alone — it is the *local* position and only the
  `common/` hierarchy resolves it.
- **Don't duplicate position components.** Adding your own
  `C_PositionGlobal3D` second on top of the auto-added one leaves one
  column stale and causes jitter.
- **No systems means no ownership.** Any code is free to write to any
  position component here — that's the coordination-by-convention part.
  The `update/` domain's systems are the ones that write velocity-driven
  updates.
