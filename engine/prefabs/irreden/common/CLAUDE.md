# engine/prefabs/irreden/common/ — position, identity, tags

Foundation layer every other domain builds on. Position, transform, name,
selection state, and the player tag. No simulation systems — those live
in `update/`.

## Key components

> **Retired (T-302):** `C_Position3D`, `C_PositionGlobal3D`,
> `C_Rotation`, and the `SYSTEM_GLOBAL_POSITION_3D` writer were
> deleted in T-302. The canonical replacement is the
> `C_LocalTransform` / `C_WorldTransform` SQT pair below.
> Ephemeral per-frame deltas (idle bob, gizmo nudges) travel through
> the `TRANSFORM_TRANSLATION` modifier field — see
> [`transform_modifier_fields.hpp`](transform_modifier_fields.hpp)
> and `SYSTEM_PROPAGATE_TRANSFORM`.

- `C_LocalTransform` / `C_WorldTransform` — canonical SQT transform
  pair. **Both auto-added by `createEntity(...)`**. See
  [SQT transform pair + propagation](#sqt-transform-pair--propagation)
  below. `C_LocalTransform::unbounded_` opts into sub-trixel
  translation; only meaningful when paired with
  `C_RotationMode::DETACHED` (GRID-mode entities snap to world cells
  regardless).
- `C_RotationMode` (Epic C C2) — `enum class RotationMode { GRID,
  DETACHED, MAIN_CANVAS_SO3 }`. GRID (default) puts the entity in the
  shared world voxel pool with grid-quantized rotation; DETACHED lives
  in a per-entity `C_EntityCanvas` whose voxel emit bakes the entity's
  full SO(3) rotation directly (T-295, via `PROPAGATE_CANVAS_ROTATION` →
  `C_CanvasLocalRotation`) — the composite stage just places the canvas.
  MAIN_CANVAS_SO3 (#1272 PR-A) carries a full SO(3) rotation like
  DETACHED but allocates no canvas — the entity renders rotated on the
  *shared* main canvas (shared textures/lighting/depth). **The mode
  value + plumbing (spawn, `setMode`, Lua surface) land first; the
  main-canvas voxel-to-trixel raster that consumes the mode is in
  progress (#1299/#1300).** Attached by
  `IRPrefab::Prefab::spawnPrefab` (default GRID, or
  `rotation_mode = IRComponent.RotationMode.DETACHED` in the prefab
  table — the enum value, not the string). Mutate at runtime with
  `IRPrefab::RotationMode::setMode(entity, newMode, name, size)` —
  allocates or destroys the canvas as needed (GRID and MAIN_CANVAS_SO3
  are canvas-free). Non-prefab entities without the component are
  implicitly GRID.
- `C_ChunkMembership` — which streaming chunk an entity belongs to
  (Epic E / `IRPrefab::Chunk::ChunkKey`). **NOT auto-added** —
  single-chunk creations carry no chunk metadata. Attached by the
  chunk-membership migration system when a creation opts into world
  streaming via `IRWorld::ChunkResidencyManager`. Design contract:
  [`docs/design/world-streaming.md`](../../../../docs/design/world-streaming.md).
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

`PROPAGATE_TRANSFORM` in
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

**Pipeline placement.** Register `PROPAGATE_TRANSFORM`
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
  **transient** modifier onto an entity (use `ticksRemaining` for decay).
  `pushGlobal(...)` targets the singleton; `pushLambda(...)` writes the
  escape-hatch component. All three reject `kInvalidFieldId` defensively.
  Use `push` for one-shot effects (screen-shake, triggered pulses). Use
  `upsertBySource` / `upsertBySourceInPlace` for steady-state writers.
- `upsertBySource(target, field, kind, param, source)` — **steady-state
  writer-owned slot** API. Slot key is `(source, field, kind)`. On hit:
  overwrites `param_` and resets `ticksRemaining_ = -1` (no decay). On
  miss: appends with `ticksRemaining_ = -1`. No `MODIFIER_DECAY`
  dependency; the slot persists until `removeBySource` or source
  destruction. `upsertBySourceGlobal(...)` targets the singleton.
- `upsertBySourceInPlace(mods, field, kind, param, source)` — same slot
  semantics as `upsertBySource` but takes a `C_Modifiers&` directly
  (caller is a system tick that already holds the archetype reference);
  skips `getComponentOptional` and the field-type check. Canonical for
  system ticks that push the same slot every frame.
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

Inline-apply factory: `IRPrefab::Modifier::applyVec3ModifierTo<
TargetComponent, Member>(name, field)` in
[`modifier_apply.hpp`](modifier_apply.hpp) generalizes the
retired-in-T-300-Phase-2 `APPLY_POSITION_OFFSET` shape — *iterate
`<TargetComponent, C_Modifiers>`, compose one vec3 field against a
`vec3(0)` base, ADD the result to a vec3 member of the target
component*. Use it for any per-frame additive vec3 channel with
exactly one consumer where global-modifier integration is not
required. Channels with multiple readers, global-modifier needs, or
multiplicative apply semantics belong on the structured-resolver
path (`MODIFIER_RESOLVE_GLOBAL` / `_EXEMPT` + read from
`C_ResolvedFields`). See `docs/design/modifiers.md` §"Inline-apply
pattern" for the discriminator and rationale.

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

### Typed fields: scalar vs vec3 vs quat

Fields are typed at registration time. `IRPrefab::Modifier::registerField`
declares a scalar field; `registerFieldVec3` declares a vec3 field;
`registerFieldQuat` declares a quaternion field. `fieldType(id)` returns
`FieldValueType::{SCALAR,VEC3,QUAT}`. The `push` overload set is
type-driven: `push(target, field, kind, float, ...)` routes into
`C_Modifiers::modifiers_` (scalar), `push(target, field, kind,
IRMath::vec3, ...)` routes into `C_Modifiers::modifiersVec3_`, and
`push(target, field, kind, IRMath::vec4, ...)` routes into
`C_Modifiers::modifiersQuat_`. Pushing the wrong type against a typed
field silently no-ops (caller bug — wrong-type push doesn't corrupt the
resolved-field storage). The same applies to `pushGlobal`.

Compose semantics for vec3 mirror the scalar path component-wise:
`ADD`/`MULTIPLY`/`SET` apply per-axis in push-order; `OVERRIDE`
replaces the entire vec3 and short-circuits prior ops; `CLAMP_MIN`/
`CLAMP_MAX` bound each axis independently, always last. The compose
helper is `composeForFieldVec3`; the per-frame resolver systems
(`MODIFIER_RESOLVE_GLOBAL`, `MODIFIER_RESOLVE_EXEMPT`) iterate both
scalar and vec3 vectors on the same `C_Modifiers` /
`C_GlobalModifiers` archetype and write to the matching scalar /
vec3 vector on `C_ResolvedFields`.

Quat compose follows the engine's quaternion convention
(`IRMath::vec4(qx, qy, qz, qw)`, identity `vec4(0, 0, 0, 1)`) and the
non-commutative nature of quaternion multiplication:

- `MULTIPLY` → **left-multiply, post-rotate**:
  `resolved = mod * base` via `IRMath::quatMul(mod.param_, value)`.
  Stacked MULTIPLYs apply outer-first in push-order: for `[r1, r2, r3]`,
  `resolved = r3 * r2 * r1 * base`. Consumers using the
  `quatMul(parent_world, local)` bone-chain idiom are post-rotating in
  the same direction.
- `OVERRIDE` → replace value; latest OVERRIDE wins across the
  combined `(globals ++ entity_mods)` sequence; everything earlier is
  discarded (same short-circuit semantics as scalar/vec3).
- `SET` → replace value in push-order (no short-circuit).
- `ADD` / `CLAMP_MIN` / `CLAMP_MAX` → not meaningful on a unit
  quaternion. The push API fires `IR_ASSERT` in debug and silently
  skips in release; the compose path also defensively drops them so
  direct-vector construction can't slip nonsense through. A future
  "clamp angle around an axis" variant would land as a separate
  `CLAMP_ANGLE_AXIS` kind.

The compose helper is `composeForFieldQuat`; the resolver systems
iterate the quat vector alongside scalar and vec3 on the same
`C_Modifiers` archetype. The compose pass normalizes the final
resolved quat **once** at the end (gate: only if any modifier touched
the value — identity-only fast path skips the normalize and returns the
caller's base unchanged, so callers passing a non-unit `baseValue` to
`applyToFieldQuat` see it round-trip when no modifier is active).

`C_ResolvedFields` carries three parallel vectors: `fields_` (scalar),
`fieldsVec3_` (vec3), and `fieldsQuat_` (quat). Read with `get(field)` /
`getVec3(field)` / `getQuat(field)`; seed with `reset(field, base)` /
`resetVec3(field, base)` / `resetQuat(field, base)`. A scalar, vec3, and
quat field id may share the same name but are distinct `FieldBindingId`s,
so their resolved values live in separate slots.

`LambdaModifier` stays scalar-only in v1 — `C_LambdaModifiers` does
not have vec3 or quat counterparts. A vec3 or quat lambda channel is a
Phase 2 follow-up if a per-frame procedural rotation curve (rather
than the structured `MULTIPLY` / `OVERRIDE` / `SET` modifiers covered
above) is needed.

Key invariants the design rests on:

- `Modifier`, `ModifierVec3`, and `ModifierQuat` all stay
  **trivially-copyable**. Anything needing inline `std::function` or
  `std::string` belongs in `C_LambdaModifiers`, not `C_Modifiers`.
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

- **`createEntity` always adds `C_LocalTransform` and
  `C_WorldTransform`.** The canonical rendered position lives in
  `C_WorldTransform.translation_`, composed by
  `SYSTEM_PROPAGATE_TRANSFORM` from `C_LocalTransform` plus the
  parent chain and the `TRANSFORM_TRANSLATION` / `TRANSFORM_SCALE`
  modifier-resolved fields.
- **Don't duplicate transform components.** Adding your own
  `C_LocalTransform` or `C_WorldTransform` second on top of the
  auto-added one leaves one column stale and causes jitter.
  `createEntity(...)` detects user-supplied `C_LocalTransform` /
  `C_WorldTransform` and skips the matching default; passing any
  other auto-attached component twice is still a footgun.
- **No systems means no ownership.** Any code is free to write to any
  position component here — that's the coordination-by-convention part.
  The `update/` domain's systems are the ones that write velocity-driven
  updates.
