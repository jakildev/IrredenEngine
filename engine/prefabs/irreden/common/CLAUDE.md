# engine/prefabs/irreden/common/ — position, identity, tags

Foundation layer every other domain builds on. Position, transform, name,
selection state, and the player tag. No simulation systems — those live
in `update/`.

## Key components

- `C_Position3D` — local vec3 (legacy; superseded by `C_LocalTransform`,
  retired in the T-199 migration).
- `C_PositionGlobal3D` — world-space vec3. **Auto-added by `createEntity(...)`**
  (legacy parallel to `C_WorldTransform`; retired with `C_Position3D`).
  Ephemeral per-frame deltas (idle bob, gizmo nudges) travel through
  the modifier framework's `POSITION_OFFSET_3D` vec3 field rather
  than a dedicated component — see [`position_modifier_fields.hpp`](position_modifier_fields.hpp)
  and the `APPLY_POSITION_OFFSET` system.
- `C_Rotation` — Euler vec3 (legacy; superseded by the quat field on
  `C_LocalTransform`).
- `C_LocalTransform` / `C_WorldTransform` — canonical SQT transform
  pair. **Both auto-added by `createEntity(...)`**. See
  [SQT transform pair + propagation](#sqt-transform-pair--propagation)
  below.
- `C_Name` — debug/display string.
- `C_Player` — tag for player-controlled entities.
- `C_Selected` — tag for UI selection.
- `C_Modifiers` / `C_GlobalModifiers` / `C_NoGlobalModifiers` /
  `C_LambdaModifiers` / `C_ResolvedFields` — generic modifier
  framework. See [Modifier framework](#modifier-framework) below.

## Systems

None. Position math is read-only in `common/` — write paths live in
`update/systems/` (velocity, physics, animation, transform propagation).

## SQT transform pair + propagation

`C_LocalTransform` holds the entity's transform relative to its parent
in the `CHILD_OF` graph. `C_WorldTransform` holds the resolved
world-space transform after parent-chain composition. Both are SQT
(scale, quat-rotation, translation) with the engine's canonical
quaternion layout `IRMath::vec4(qx, qy, qz, qw)` — identity is
`vec4(0, 0, 0, 1)`. See `engine/math/CLAUDE.md` "Quaternions" for the
algebra contract (`IRMath::quatMul`, `IRMath::rotateVectorByQuat`).

`SYSTEM_PROPAGATE_TRANSFORM` in
[`update/systems/system_propagate_transform.hpp`](../update/systems/system_propagate_transform.hpp)
walks the parent chain in topological order each tick and writes
`C_WorldTransform`. The composition formula:

```
world.scale       = parent.world.scale * local.scale * modifier_scale
world.rotation    = quatMul(parent.world.rotation, local.rotation)
world.translation = parent.world.translation
                  + rotateVectorByQuat(parent.world.scale * local.translation,
                                       parent.world.rotation)
                  + modifier_translation
```

Roots (no `CHILD_OF`, or parent's archetype lacks `C_WorldTransform`)
use identity as the parent transform.

**Modifier integration.** Per-frame perturbations (shake, recoil,
wobble, animation-blend overlays) push vec3 modifiers under the
`TRANSFORM_TRANSLATION` / `TRANSFORM_SCALE` fields registered in
[`transform_modifier_fields.hpp`](transform_modifier_fields.hpp). The
propagation system reads the modifier-resolved values from
`C_ResolvedFields` and folds them into the world transform per the
formula above. Default fallbacks when no resolved field exists:
translation `vec3(0)`, scale `vec3(1)` — i.e., no perturbation.
Entities that don't push perturbations don't need `C_Modifiers`. The
matching `ROTATION` quat field arrives with the quat modifier kind
ticket (T-198); until then, `modifier_rotation` is identity.

**Pipeline placement.** Register `SYSTEM_PROPAGATE_TRANSFORM`
after the modifier resolver pipeline so the resolved fields are
current, and before any consumer (render, gizmo, physics) that reads
`C_WorldTransform`.

**Topological order is non-negotiable.** Iterating an archetype in
arbitrary order computes stale-parent results for any non-root entity.
The propagation system topo-sorts candidate archetype nodes per tick
(by parent-chain depth) and processes them parents-first. Cost is
O(N + passes × archetypes); passes ≤ tree depth.

**`setParent` during a tick.** If a system calls `setParent` after the
propagation step has run, the new child won't see its parent's
transform until the next frame. This matches the existing "structural
changes during iteration" rule — defer the relation change to a frame
boundary if the visual must update the same frame.

**Auto-attach + caller-supplied conflict.** `createEntity(...)`
auto-attaches default `C_LocalTransform` and `C_WorldTransform`
alongside `C_PositionGlobal3D`. The free function detects when the
caller passes one of these types explicitly and skips the default for
that type, so `createEntity(C_LocalTransform{vec3(5,6,7)})` produces an
entity whose local translation is `(5,6,7)`, not the default `(0,0,0)`.
Without that guard the duplicate type would emplace a second column
row, leaving the caller's value orphaned at `row + 1`.

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
  column stale and causes jitter. `createEntity(...)` detects
  user-supplied `C_PositionGlobal3D` / `C_LocalTransform` /
  `C_WorldTransform` and skips the matching default; passing any
  other auto-attached component twice is still a footgun.
- **No systems means no ownership.** Any code is free to write to any
  position component here — that's the coordination-by-convention part.
  The `update/` domain's systems are the ones that write velocity-driven
  updates.
