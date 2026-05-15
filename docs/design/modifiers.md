# Modifier framework

Engine-level mechanism that lets any component field be modulated by a
vector of structured `(transform, parameter)` pairs. The pattern
generalizes the original `local + offset → global` position pipeline
so other features (velocity drag, buff/debuff stacks, color
modulation, etc.) can hand off the "compose base + N modifications →
effective value" math to a shared resolver. T-192 migrated the
per-entity offset channel itself onto the framework — see
`POSITION_OFFSET_3D` at the bottom of this doc.

This document specifies the framework's public contract and the
locked design choices behind it. The runtime that backs these
contracts (registry, resolver systems, source-destruction sweep)
ships in the follow-up task — see [Decomposition](#decomposition).

## Table of contents

- [Vocabulary](#vocabulary)
- [Locked design choices](#locked-design-choices)
- [Data shapes](#data-shapes)
- [Resolver pipeline](#resolver-pipeline)
- [Public API surface](#public-api-surface)
- [Existing-pattern audit](#existing-pattern-audit)
- [Decomposition](#decomposition)

## Vocabulary

- **Field** — a logical scalar value an entity owns, identified by a
  dense `FieldBindingId` (e.g. *velocity X drag scale*, *color V
  channel*, *position Z offset*). The framework is agnostic to where
  the field's base value lives — it only operates on the
  `(field, base) → effective` transformation.
- **Modifier** — a request to transform one field's value by one
  amount (`ADD 0.25`, `MULTIPLY 0.8`, `CLAMP_MIN 0.0`, …). Carries a
  source attribution and an optional decay tick counter.
- **Source** — the entity that pushed the modifier. When the source
  is destroyed, every modifier it pushed is swept away.
- **Resolver** — the pipeline stage that consumes the modifier vector,
  produces a per-field effective value, and writes it to the output
  cache (`C_ResolvedFields`) or directly returns it via the
  `applyToField` query API.

## Locked design choices

The design picks below were settled during the planning rounds for
issue #302 and are NOT open for re-litigation in implementation tasks
(children 2-5). If during implementation a choice looks wrong,
**escalate** rather than silently re-deciding — see
[Notes for implementers](#notes-for-implementers).

### Hybrid transform shape (Path C)

`TransformKind` is a small structured enum (`ADD`, `MULTIPLY`, `SET`,
`CLAMP_MIN`, `CLAMP_MAX`, `OVERRIDE`). The lambda escape hatch is a
**sibling component** (`C_LambdaModifiers`) so the structured
modifier stays trivially-copyable and dense-cache-friendly.

**Why:** the long-tail "I need a sine-decayed source-aware modifier"
case exists, but it is rare. Forcing a `std::function` into the dense
modifier path penalizes every entity in the cache to support a
minority pattern. The split keeps the hot path tight while leaving
the escape hatch one component-add away.

### Eager composition, no dirty flag

The resolver runs every UPDATE tick over a dense archetype iteration.
Recompute is `O(modifiers per entity)`; for typical entities (~5
modifiers) that's < 1 cache line of work per entity.

**Why:** dirty-flag accounting costs branch + flag-store on every
modifier push and decay, and saves work only when modifier vectors
are mostly stable across frames. Profiling other ECS engines that
attempted dirty-flag modifier pipelines showed branch cost dominating
the savings. The ECS storage is designed for dense iteration; lean
into it.

### Vector-on-component storage

`C_Modifiers` carries `std::vector<Modifier>` directly. One
allocation per entity, all modifiers contiguous, trivially-copyable
element type.

**Why:** entity-per-modifier (one entity per modifier with `EntityId
target_`) is cleaner conceptually but pays an entity creation +
archetype lookup per push, and forces the resolver to do an
N×M relation walk instead of a dense column iteration. Vector-on-
component is the cheaper end of the cost curve for v1. Modifier-as-
entity remains a reasonable v2 path for cases that genuinely need it
(modifier with its own physics / particle effect / lifetime), but it
doesn't earn its complexity for the common case.

### `EntityId` source attribution (not strings, not handles)

Each modifier carries `EntityId source_`. The source's identity is
the entity itself.

**Why:** engine-native, dense, no hash map, no string interning.
Human-readable names come for free via the source entity's `C_Name`
component. Source-tied lifetime falls out naturally — when the source
dies, sweep modifiers where `source_ == destroyedId`. No separate
"modifier handle" type needed; you can either remember the source
yourself or call `removeBySource(sourceId)`.

### Source attribution lifetime contract

The source-destruction sweep MUST run inside `EntityManager::destroyEntity`
before `returnEntityToPool` fires. Deferred sweep is unsafe: `EntityId` has
no generation counter, so after `returnEntityToPool` the same id can be
issued to an unrelated entity, and a delayed `removeBySource(oldId)` would
silently strip that new entity's modifiers. T-050 implements a manual sweep
path (`removeBySource` must be called by callers before `destroyEntity`); the
pre-destroy hook that guarantees no stale modifiers survive `EntityId` reuse
is tracked in #340.

### `ticksRemaining_` decay only

Built-in lifetime is just an `int32_t` tick counter and a one-time
sweep of expired modifiers (`ticksRemaining_ <= 0` after decrement).
`-1` is the sentinel for "no decay".

**Why:** non-linear / curved / source-driven decay is **not the
modifier's problem**. The source entity's tick function adjusts its
modifier's `param_` directly via the source attribution, or
destroys-and-replaces. Trying to bake every decay shape into the
modifier struct turns it into a small interpreter; we already have a
better interpreter — the host language and the source entity's tick
function.

### Singleton globals + archetype-routed exemption

A singleton entity carries `C_GlobalModifiers`. Per-entity opt-out is
a `C_NoGlobalModifiers` empty tag. Resolver dispatch is via
**archetype routing**: register the global-resolving system with a
filter that excludes archetypes containing `C_NoGlobalModifiers`, and
register a sibling exempt-resolving system that includes the tag.
Both are dense iterations; neither branches on the tag at runtime.

**Why:** "all entities feel the global slow except boss enemies" is
the canonical case. Putting the exemption check inside the resolver
tick body adds a branch to every entity-tick to support a small
minority. Archetype routing solves the same problem in the type
system: the entities are already in different archetypes, so use the
ECS's native dispatch instead of branching.

### Vector ordering — push-order, OVERRIDE wins last

The resolver applies modifiers in the order they appear in the
vector. `OVERRIDE` short-circuits to the most recent OVERRIDE's
`param_` and ignores everything after it.

**Why:** the user-facing semantics most callers expect ("the last
buff I pushed wins") map to push-order without any sort. `OVERRIDE`
is the explicit "this beats whatever came before" knob; the
implementation looks for the latest OVERRIDE in one pass, then
applies the prefix-of-the-vector before that override (so it can
still be clamped by a later CLAMP_MIN/MAX).

## Data shapes

```cpp
namespace IRComponents {

using FieldBindingId = std::uint16_t;        // dense registry index
constexpr FieldBindingId kInvalidFieldId = 0;  // 0 is reserved

enum class TransformKind : std::uint8_t {
    ADD,
    MULTIPLY,
    SET,
    CLAMP_MIN,
    CLAMP_MAX,
    OVERRIDE,
};

struct Modifier {
    FieldBindingId       field_;          // 2 B — what is being modified
    TransformKind        kind_;           // 1 B
                                          // 1 B compiler-inserted pad
    float                param_;          // 4 B — amount / multiplier / set / bound
    IREntity::EntityId   source_;         // 8 B — who pushed this
    std::int32_t         ticksRemaining_; // 4 B — -1 = no decay; ≤ 0 = expired
                                          // 4 B tail pad (alignof 8)
};
// Trivially-copyable. sizeof == 24 B, alignof == 8 (locked by unit test).

struct LambdaModifier {
    FieldBindingId              field_;
    std::function<float(float)> fn_;             // base → effective
    IREntity::EntityId          source_;
    std::int32_t                ticksRemaining_;
};
// NOT trivially-copyable. Lives in a sibling component on purpose.

struct ResolvedField {
    FieldBindingId field_;
    float          value_;
};

struct C_Modifiers        { std::vector<Modifier>       modifiers_; };
struct C_GlobalModifiers  { std::vector<Modifier>       modifiers_; };  // singleton
struct C_NoGlobalModifiers {};                                          // empty tag
struct C_LambdaModifiers  { std::vector<LambdaModifier> modifiers_; };
struct C_ResolvedFields   { std::vector<ResolvedField>  fields_; };
}
```

`Modifier` is 24 B (not the 16 B figure floated during planning); the
8-byte `EntityId` source dominates the alignment, so the struct ends up
two-per-cache-line — which was the property that mattered. A future
v2 packing experiment could pull `source_` down to a 32-bit raw entity
id (low 32 bits of `EntityId`; the high bits are flag metadata) and
collapse the struct to 16 B / three-per-cache-line; out of scope for
v1 since the runtime should drive that representation choice.

## Resolver pipeline

Once per pipeline execution, end of UPDATE phase, before RENDER reads:

```
ModifierDecay              decrement ticksRemaining_, drop expired
GlobalModifierDecay        same on the singleton
LambdaModifierDecay        same on C_LambdaModifiers (also runs lambda dtors)
ModifierResolveGlobal      non-exempt entities (archetype filter)
ModifierResolveExempt      exempt entities (with C_NoGlobalModifiers)
ModifierResolveLambda      lambda escape hatch, both archetypes
→ RENDER reads C_ResolvedFields directly
```

Six systems, all dense archetype iterations, no per-entity hash
lookups in any tick body. Resolver dispatch is archetype-routed:
`ModifierResolveGlobal` is registered with an *exclude*
`C_NoGlobalModifiers` filter; `ModifierResolveExempt` with an
*include* filter. Both are dense; neither branches on the tag.

### Resolver evaluation order (per entity, per field)

1. Start with the field's `base` value (read from the field's owning
   component — provided to the framework via the field-binding
   registry).
2. Look back through the entity's modifier vector for the **latest**
   `OVERRIDE` for this field. If found, replace `base` with that
   override's `param_` and discard every modifier earlier than it.
   `OVERRIDE` discards prior `CLAMP_MIN`/`CLAMP_MAX` as well as prior
   `ADD`/`MULTIPLY`/`SET`. To clamp an `OVERRIDE` result, push the clamp
   *after* the `OVERRIDE`.
3. Apply `ADD` / `MULTIPLY` / `SET` modifiers in push-order (newer
   wins for `SET`).
4. Apply `CLAMP_MIN` / `CLAMP_MAX` (always after the algebra so they
   bound the result).
5. Write the result to `C_ResolvedFields` (or return it from
   `applyToField`).

`SET` is distinct from `OVERRIDE` in that `SET` participates in
later clamps; `OVERRIDE` short-circuits the prefix.

## Public API surface

The framework's free-function API ships in child 2 alongside the
runtime. The shape (locked here so future migration tasks can plan around it):

```cpp
namespace IRPrefab::Modifier {

// Field registry (called at init by feature owners).
IRComponents::FieldBindingId registerField(const char* name);
const char* fieldName(IRComponents::FieldBindingId id);
std::size_t fieldCount();

// Push.
void push(IREntity::EntityId target,
          IRComponents::FieldBindingId field,
          IRComponents::TransformKind kind,
          float param,
          IREntity::EntityId source,
          std::int32_t ticksRemaining = -1);

void pushGlobal(IRComponents::FieldBindingId field,
                IRComponents::TransformKind kind,
                float param,
                IREntity::EntityId source,
                std::int32_t ticksRemaining = -1);

void pushLambda(IREntity::EntityId target,
                IRComponents::FieldBindingId field,
                std::function<float(float)> fn,
                IREntity::EntityId source,
                std::int32_t ticksRemaining = -1);

// Remove.
void removeBySource(IREntity::EntityId source);

// Direct query (skips the C_ResolvedFields cache).
float applyToField(IREntity::EntityId target,
                   IRComponents::FieldBindingId field,
                   float baseValue);

// Pipeline registration (called once at init).
void registerResolverPipeline();
}
```

`registerField` stores the pointer, not a copy — `name` must have
static-storage lifetime (use a string literal). `applyToField` and the
resolver pipeline share a single per-field evaluator; T-050 should verify
equivalence with a property test so the two read paths never silently
disagree. `C_ResolvedFields` lookup by `FieldBindingId` is linear over
`std::vector` — fine for v1 (~5 fields per entity), but T-050 should pick a
lookup strategy explicitly and note it in the runtime PR.

Lua mirrors this (child 4): `ir.modifier.registerField`,
`ir.modifier.push`, `ir.modifier.pushGlobal`, `ir.modifier.pushLambda`,
`ir.modifier.removeBySource`, `ir.modifier.applyToField`. The
`TransformKind` enum is exposed as
`ir.modifier.ADD/MULTIPLY/SET/CLAMP_MIN/CLAMP_MAX/OVERRIDE`.

## Existing-pattern audit

The engine already hand-rolls the "base + modulation → effective"
shape in several places. Both v1 migration candidates were deferred
after implementation-phase analysis; the framework's first real
consumers will come from downstream game logic.

### v1 migration targets — deferred with rationale

Both candidates identified during planning were deferred after
implementation-phase analysis (see issue #305 discussion and PR #332
architect review). The framework (T-050, T-052) is complete and tested;
its first real consumers arrive via downstream game logic.

- **Position pattern** — deferred. Three concrete blockers found during
  analysis:
  1. `CHILD_OF` inheritance: `system_update_positions_global` reads the
     parent's `C_PositionGlobal3D` via `RelationParams<...>{Relation::CHILD_OF}`.
     The modifier resolver has no relation-walking; keeping inheritance
     requires keeping the legacy system, which defeats the migration.
  2. Vec3 vs flat-scalar: `C_ResolvedFields` holds
     `std::vector<{FieldBindingId, float}>`. Writing back three correlated
     position fields costs more overhead than the offset addition it replaces.
  3. The existing pattern is already minimal: `system_apply_position_offset`
     is ~30 lines of focused, well-fitted code the framework cannot shrink.

- **Velocity drag** — deferred. The hover/blend phase in
  `system_velocity_drag.hpp` is the system's core value for its private
  consumer. That phase requires stateful-lambda support plus a
  `LAMBDA_MODIFIER_DECAY` system — neither exists yet (see #341).
  Splitting the simple `MULTIPLY` part out while leaving hover/blend in
  the legacy system produces two systems with split state models, which is
  worse than the status quo. Migration is viable only after the framework
  grows stateful-lambda support.

  **Do not delete or declare this system dormant without checking private
  creations under `creations/<gitignored>/`** — a private consumer
  registers `VELOCITY_DRAG` and would break silently (see #338 for the
  engine-wide process rule).

### Framework gaps — follow-up issues

Three gaps surfaced during T-050 runtime development. All three ship in
master after T-060, T-061, and T-062 (Part 1) land; one follow-up
(stateful lambdas, #341 Part 2) remains gated on architect review.

- **#339** — Wire `MODIFIER_RESOLVE_EXEMPT` via archetype exclude-tag filter.
  Resolved by T-060: `Exclude<...>` template parameter added to
  `IRSystem::createSystem<>`, `MODIFIER_RESOLVE_EXEMPT` system shipped,
  `MODIFIER_RESOLVE_GLOBAL` updated to skip `C_NoGlobalModifiers`.
- **#340** — Pre-destroy hook for auto-sweep of source-attributed modifiers.
  Resolved by T-061: `EntityManager::registerPreDestroyHook` shipped,
  `registerResolverPipeline()` wires `removeBySource(destroyed)` so
  destroying a source entity sweeps its modifiers off live targets
  before the EntityId recycles.
- **#341 Part 2** — Stateful-lambda design. Proposal in
  `docs/design/modifier_stateful_lambdas.md`. Recommends the
  "companion component" shape (lambda receives entity ID; state lives
  on a sibling component). Needs architect lock-in before the lambda
  signature changes. Velocity-drag migration is gated on this landing.

### Follow-up candidates (post-epic)

These fit the modifier shape but are out of scope for the v1 epic.
Each gets its own follow-up task once the framework has baked.

- **Animation color** — `C_AnimColorState` + `system_animation_color.hpp`.
  HSV blending over a curve. Lambda territory (the curve isn't a
  scalar transform).
- **Spring color** — `system_spring_color.hpp`. Physics-driven color
  modulation. Lambda territory; the spring state is already a
  per-field accumulator that maps cleanly onto a `LambdaModifier`.
- **Spawn / trigger glow** — time-bounded brightness modulation on
  voxel sets. Clean `MULTIPLY` + decay. Probably the smallest
  follow-up.
- **Texture scroll** — UV offset modulation on materials. Less
  obviously a modifier (it's input to UV computation, not a
  target value), so it may stay as-is.

## Decomposition

The epic decomposes into five sequenced child tasks. Stack order:

```
1 ─► 2 ─► 3 / 4 (parallel) ─► 5
```

| Child | Title                                                 | Model    |
|-------|-------------------------------------------------------|----------|
| 1     | Design doc + audit + framework declarations           | `[opus]` |
| 2     | Core runtime (registry, resolver systems, sweep)      | `[opus]` |
| 3     | Migrate position + velocity-drag patterns (deferred — see issue #305) | `[opus]` |
| 4     | Lua bindings                                          | `[sonnet]` |
| 5     | `modifier_demo` creation (visual showcase)            | `[sonnet]` |

Children 3 and 4 may run in parallel after child 2 lands. Child 5
needs all four predecessors.

## Notes for implementers

- The locked design choices above are the source of truth. If during
  writing the runtime, a child task finds a flaw in any choice,
  **escalate** to the architect — don't silently re-decide.
- The `Modifier` struct must stay trivially-copyable. If a refinement
  requires a `std::function` or `std::string` inline, that's a smell
  — push it to a sibling component.
- Framework public API lives in `IRPrefab::Modifier::` per the
  layering principle in `engine/prefabs/irreden/render/CLAUDE.md`
  ("Exposing system public API from the prefab layer"), NOT in
  `IRRender::` or any engine-library-level namespace.
- `FieldBindingId` is a registry-backed dense integer. The registry
  itself ships in child 2. This task only declares the type alias and
  documents what the registry will look like.
- `kInvalidFieldId == 0` is reserved so that an unset binding id is
  cheaply detectable and so that `0` is never a valid registered
  field. Registration starts at id `1`.
- `push()` should defensively reject any modifier where `field_ ==
  kInvalidFieldId` — a default-constructed `Modifier{}` produces this
  state (`kind_ == ADD`, `field_ == 0`). Fail fast rather than silently
  adding an invalid modifier to the vector.

## vec3 extension (T-191)

The framework was extended in T-191 to carry vec3-typed fields
alongside the scalar path. The shape stays parallel rather than
variant-based:

- `ModifierVec3` mirrors `Modifier` with `IRMath::vec3 param_` in
  place of `float param_`. Stays trivially-copyable; `sizeof == 32`,
  `alignof == 8`.
- `ResolvedFieldVec3` mirrors `ResolvedField`. `C_Modifiers`,
  `C_GlobalModifiers`, and `C_ResolvedFields` each carry both a
  scalar vector (unchanged from v1) and a sibling vec3 vector —
  one component, two internal columns.
- The field registry tracks `FieldValueType::{SCALAR, VEC3}` per
  binding id. `registerField` defaults to SCALAR; `registerFieldVec3`
  declares a vec3 field. `push`/`pushGlobal` are overloaded on
  scalar vs `vec3` param; pushing the wrong type silently no-ops
  (caller bug, not data error).
- Compose semantics are component-wise: ADD/MULTIPLY/SET apply
  per-axis in push-order; CLAMP_MIN/CLAMP_MAX bound each axis
  independently via `IRMath::max`/`IRMath::min`; OVERRIDE replaces
  the entire vec3 and short-circuits prior ops. Resolver evaluation
  order matches the scalar path — `composeForFieldVec3` is the vec3
  twin of `composeForField`.
- Resolver systems iterate both vectors on the same archetype each
  tick (`MODIFIER_RESOLVE_GLOBAL`, `MODIFIER_RESOLVE_EXEMPT`).
  Decay systems sweep both vectors. `removeBySource` sweeps both.
- `LambdaModifier` stays scalar-only in v1. A `std::function<vec3
  (vec3)>` channel and a quat modifier kind are Phase 2 follow-ups.
- Lua surface: `IRModifier.registerFieldVec3`, `addVec3`,
  `addGlobalVec3`, `applyToFieldVec3`, `resolvedVec3` mirror the
  scalar Lua API. Param tables accept either an `IRMath::vec3`
  userdata or a `{x, y, z}` keyed table via the `vec3FromLua`
  helper.

T-192 (the first consumer) deleted `C_PositionOffset3D` and migrated
the idle-bob writer onto the `POSITION_OFFSET_3D` vec3 field
(`engine/prefabs/irreden/common/position_modifier_fields.hpp`).
`APPLY_POSITION_OFFSET` composes each bob-eligible entity's vec3
modifiers locally against a vec3(0) base and adds the result to
`C_PositionGlobal3D` — no resolver-pipeline registration is required
just for the offset channel, only `MODIFIER_DECAY` running before the
bob writer in the UPDATE pipeline. The reader-side sprite, hitbox,
canvas-to-framebuffer, gizmo-drag, and picking systems read
`C_PositionGlobal3D` directly; the `global + offset` manual sum is
gone.
