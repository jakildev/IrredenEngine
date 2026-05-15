# engine/prefabs/irreden/common/ — position, identity, tags

Foundation layer every other domain builds on. Position, transform, name,
selection state, and the player tag. No simulation systems — those live
in `update/`.

## Key components

- `C_Position3D` — local vec3.
- `C_PositionGlobal3D` — world-space vec3. **Auto-added by `createEntity(...)`**.
  Ephemeral per-frame deltas (idle bob, gizmo nudges) travel through
  the modifier framework's `POSITION_OFFSET_3D` vec3 field rather
  than a dedicated component — see [`position_modifier_fields.hpp`](position_modifier_fields.hpp)
  and the `APPLY_POSITION_OFFSET` system.
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
  dropping entries whose `source_` matches. Wired automatically
  into `EntityManager::destroyEntity` by `registerResolverPipeline()`
  via a pre-destroy hook, so destroying a source entity sweeps its
  attributed modifiers off live targets before the EntityId
  recycles. Callable directly when an "ability ends but caster
  persists" pattern needs the same sweep without destroying the
  source entity.
- `applyToField(target, field, base) → float` — direct query. Shares
  one evaluator with the resolver pipeline so the cache and direct
  paths give the same answer for the same input.
- `registerResolverPipeline()` — call once at creation init. Creates
  the singleton globals entity (named `"modifierGlobals"`) and
  registers the six resolver systems in canonical order. Returns
  the `SystemId`s in pipeline order so the caller splices them
  into its `IRTime::UPDATE` pipeline.
- `globalsEntity()` — returns the singleton globals entity created by
  `registerResolverPipeline()`. Intended for tests and diagnostics;
  production code should use `pushGlobal` / `removeBySource`.

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

The canonical visual reference is `creations/demos/modifier_demo/`:
run `fleet-run IRModifierDemo` and press keys 1–8 to see each
capability (Haste, Stun, Slow, Stack, GlobalSlow, LambdaSine,
SourceKill, Clamp) live. The HUD shows per-cube resolved speed
each tick.

### Typed fields: scalar vs vec3

Fields are typed at registration time. `IRPrefab::Modifier::registerField`
declares a scalar field; `registerFieldVec3` declares a vec3 field.
`fieldType(id)` returns `FieldValueType::{SCALAR,VEC3}`. The `push`
overload set is type-driven: `push(target, field, kind, float, ...)`
routes into `C_Modifiers::modifiers_` (scalar) and `push(target, field,
kind, IRMath::vec3, ...)` routes into `C_Modifiers::modifiersVec3_`.
Pushing the wrong scalar/vec3 against a typed field silently no-ops
(caller bug — wrong-type push doesn't corrupt the resolved-field
storage). The same applies to `pushGlobal`.

Compose semantics for vec3 mirror the scalar path component-wise:
`ADD`/`MULTIPLY`/`SET` apply per-axis in push-order; `OVERRIDE`
replaces the entire vec3 and short-circuits prior ops; `CLAMP_MIN`/
`CLAMP_MAX` bound each axis independently, always last. The compose
helper is `composeForFieldVec3`; the per-frame resolver systems
(`MODIFIER_RESOLVE_GLOBAL`, `MODIFIER_RESOLVE_EXEMPT`) iterate both
scalar and vec3 vectors on the same `C_Modifiers` /
`C_GlobalModifiers` archetype and write to the matching scalar /
vec3 vector on `C_ResolvedFields`.

`C_ResolvedFields` carries two parallel vectors: `fields_` (scalar)
and `fieldsVec3_` (vec3). Read with `get(field)` / `getVec3(field)`;
seed with `reset(field, base)` / `resetVec3(field, base)`. A scalar
field id and a vec3 field id may share the same name but are distinct
`FieldBindingId`s, so their resolved values live in separate slots.

`LambdaModifier` stays scalar-only in v1 — `C_LambdaModifiers` does
not have a vec3 counterpart. A vec3 lambda channel is a Phase 2
follow-up alongside the quat modifier kind.

Key invariants the design rests on:

- `Modifier` and `ModifierVec3` both stay **trivially-copyable**.
  Anything needing inline `std::function` or `std::string` belongs
  in `C_LambdaModifiers`, not `C_Modifiers`.
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

See `.fleet/status/modifier-runtime-gaps.md` (queue-manager-owned;
feature PRs do not edit) for the current list of pending modifier
runtime work and architect-gated decisions.

## Commands

- `command_suite_camera.hpp` — registers `ZOOM_IN`, `ZOOM_OUT`,
  `MOVE_CAMERA_*`, `CLOSE_WINDOW`. A convenience bundle creations pick
  up en masse.

## Gotchas

- **`createEntity` always adds `C_PositionGlobal3D`.** Rendered
  position is whatever lives in `C_PositionGlobal3D` after the UPDATE
  pipeline completes — `GLOBAL_POSITION_3D` writes `local + parent`
  and `APPLY_POSITION_OFFSET` folds in the modifier-resolved
  `POSITION_OFFSET_3D` vec3 field. Never infer a draw position from
  `C_Position3D` alone — it is the *local* position and only the
  `common/` hierarchy resolves it.
- **Don't duplicate position components.** Adding your own
  `C_PositionGlobal3D` second on top of the auto-added one leaves one
  column stale and causes jitter.
- **No systems means no ownership.** Any code is free to write to any
  position component here — that's the coordination-by-convention part.
  The `update/` domain's systems are the ones that write velocity-driven
  updates.
