# Modifier framework — stateful lambdas (design proposal)

> **Status:** proposal. Implementation gated on architect lock-in.
> **Tracks:** issue #341 Part 2.
> **Companion docs:** `docs/design/modifiers.md`,
> `engine/prefabs/irreden/common/CLAUDE.md` "Open follow-ups".

## Context

`LambdaModifier::fn_` is `std::function<float(float)>` today — a pure
`base → effective` transform. The lambda owns no state across frames.

That signature is enough for one-shot algebra (e.g. `[](float v) { return v * 0.8f; }`)
but blocks every pattern that needs per-modifier mutable state across
ticks:

- **Velocity drag's hover/blend phase** (`system_velocity_drag.hpp`):
  needs an elapsed-time counter to interpolate between `dragOnHover_`
  and `dragPostHover_` over `blendTicks_`. The state is *per active
  drag*, not per entity — two simultaneous drags from two sources need
  independent timers.
- **Spawn / trigger glow's hold + fade phases**: needs a phase enum
  (`HOLD` vs `FADE`) plus an elapsed counter, both per glow instance.
- **Animation color / spring color**: spring physics needs persistent
  velocity + position state per modifier, refreshed each tick.

The existing migration tasks for these patterns (#305 follow-ups, plus
the spring/glow targets in `docs/design/modifiers.md` §"Follow-up
candidates") are blocked on this design landing.

## Architect-stated preference

Issue #341 lists three viable shapes and identifies (c) as preferred:

- **(a) Per-lambda state struct** — `void* state_` (or `shared_ptr<void>`)
  on `LambdaModifier`. Lambda owns its state. Casts at the lambda
  boundary; type erasure leaks across the framework. Rejected: turns
  the framework into a poor-man's runtime polymorphism layer.
- **(b) Templated `LambdaModifier<TState>`** — state typed in. Better
  type safety but requires `C_LambdaModifiers` to hold a polymorphic
  collection (currently `std::vector<LambdaModifier>` is monomorphic).
  Rejected: complicates dense iteration, breaks the
  trivially-addressable archetype column.
- **(c) Lambda + companion component** — state lives on the entity in
  its own component; lambda receives the entity ID and reads its
  companion. Most ECS-native; preferred.

This proposal fleshes out (c).

## Proposal — companion-component shape

### Data shapes

```cpp
namespace IRComponents {

// Lambda signature gains the modifier's source EntityId. Callers who
// don't need state ignore it.
struct LambdaModifier {
    FieldBindingId                                              field_;
    std::function<float(float baseValue, IREntity::EntityId source)> fn_;
    IREntity::EntityId                                          source_;
    std::int32_t                                                ticksRemaining_;
};

} // namespace IRComponents
```

The companion component is **whatever component the source entity
already owns or chooses to add**. The framework neither defines the
shape nor manages its lifetime. The lambda body looks up its companion
through the supplied `source` id:

```cpp
// Velocity drag, post-migration. State lives in C_VelocityDragState
// on the drag's source entity.
auto fn = [field](float velocityScalar, IREntity::EntityId source) -> float {
    auto *state = IREntity::getComponentOptional<C_VelocityDragState>(source)
                      .value_or(nullptr);
    if (!state) return velocityScalar;             // companion gone — pass through
    state->elapsed_ += IRTime::deltaTime(IRTime::UPDATE);
    float t = std::clamp(state->elapsed_ / state->blendTicks_, 0.0f, 1.0f);
    float drag = std::lerp(state->dragOnHover_, state->dragPostHover_, t);
    return velocityScalar * drag;
};
IRPrefab::Modifier::pushLambda(target, field, fn, source, /*ticksRemaining=*/-1);
```

The lambda mutates the companion (`state->elapsed_ += ...`) — that's
the "stateful" part. The framework still treats the lambda as a pure
`base → effective` function; the mutation is the lambda's private
business.

### Lifetime contract

Companion lifetime is the **caller's responsibility**, exactly the
same way `pushLambda` already requires the lambda's captured state to
outlive the modifier:

- The caller adds the companion when it pushes the lambda.
- The caller removes the companion when it removes the lambda
  (`removeBySource` already sweeps lambdas; an `onPreDestroy` hook
  per #340 will sweep automatically).
- If the lambda fires after its companion is gone, the lambda
  defensively returns `baseValue` unchanged (see the snippet above).

The framework provides **no helper** for companion-component
management. Adding one would force a particular shape (single
component? per-modifier component? state entity?) — let consumers pick
the shape that fits their use case.

### Per-active-instance state (multiple drags from multiple sources)

For patterns where the *same source* pushes multiple modifiers and
each needs independent state (rare — usually one source = one
modifier), the companion component must itself hold a vector keyed by
field id:

```cpp
struct C_VelocityDragState {
    struct PerField {
        FieldBindingId field_;
        float elapsed_;
        float blendTicks_;
        float dragOnHover_;
        float dragPostHover_;
    };
    std::vector<PerField> entries_;
};
```

The lambda then looks up its row by `field_`. Linear scan over a
short vector matches the framework's existing `findResolvedField`
pattern; if the row count grows, swap to a small flat-map (same
escalation path documented in `component_modifiers.hpp`).

For the common case (one source, one modifier, one state struct), the
companion is a flat struct and no per-field lookup is needed.

## Migration impact

### Lambda signature change is breaking

Every existing `pushLambda` call site receives a one-arg lambda. The
signature change to two-arg requires updating:

- `engine/prefabs/irreden/common/modifier.hpp` — `pushLambda`
  parameter type.
- `engine/prefabs/irreden/common/components/component_modifiers.hpp`
  — `LambdaModifier::fn_` field type, `C_ResolvedFields::applyLambda`
  helper signature.
- `engine/prefabs/irreden/common/systems/system_modifier_resolve_lambda.hpp`
  — call site invokes `fn_(rf.value_, lambda.source_)`.
- `engine/prefabs/irreden/common/modifier_lua.hpp` — Lua binding's
  thunk wrapping `sol::function`.
- `test/script/modifier_lua_test.cpp`, `test/ecs/modifier_runtime_test.cpp`
  — fixtures that construct `LambdaModifier` directly need the
  two-arg lambda.

Existing in-tree call sites: search shows the only consumers are the
test fixtures and the Lua binding. **No production lambda exists
today** that would break (the framework's only consumer is the
modifier_demo creation per #307, which has not yet landed). The
breaking-change cost is therefore close to zero — do it cleanly in one
PR rather than introducing a parallel `pushLambdaStateful` overload.

### Lua-side equivalence

The Lua binding's lambda thunk needs to forward the entity id into
the `sol::function` call:

```cpp
auto fn = [callable = std::move(callable)](float baseValue,
                                           IREntity::EntityId source) -> float {
    sol::protected_function_result r = callable(baseValue, static_cast<lua_Integer>(source));
    if (!r.valid()) {
        // log and return baseValue (existing error path)
        return baseValue;
    }
    return r.get<float>();
};
```

Lua-side: `ir.modifier.pushLambda(target, { ..., fn = function(base, source) ... end })`.
The `source` is the entity id the Lua script can use to query
companion components (assuming a Lua getter like
`ir.entity.getComponent(source, ir.components.VelocityDragState)`).

### Decay still works

LAMBDA_MODIFIER_DECAY is unchanged — it operates on `ticksRemaining_`,
which is independent of the lambda signature. When a stateful lambda
expires via decay, its `std::function` destructor fires (as today),
releasing any captured state. The companion component on the source
entity is NOT swept — that's the caller's job (companion ownership
sits outside the framework on purpose).

## Open questions for the architect

1. **Should the framework provide a `C_LambdaState<T>` helper?**
   Templated wrapper that handles the lookup-by-source pattern so
   lambda bodies don't repeat the `getComponentOptional + nullptr
   check` boilerplate. Pro: ergonomic, less repetition. Con: forces a
   specific shape; the framework starts owning state semantics it
   should stay neutral about. Recommendation: **no**, leave it to
   consumers, but call it out in CLAUDE.md so the pattern is visible.

2. **Should `removeBySource` also remove the companion component?**
   Pro: keeps lambda + state symmetric — one remove call cleans up
   both. Con: the framework has no idea what companion type to
   remove; it would need a registry of `(source-pattern → companion-
   type)` pairs, which is exactly the shape (a) tried to avoid.
   Recommendation: **no**. Companion lifecycle stays caller-owned;
   the source entity's owning system handles companion teardown when
   it tears down the source.

3. **Should the lambda signature also receive `ticksRemaining_` or
   `field_`?** Could let lambdas adapt their behavior across their
   own decay window without a companion lookup. Pro: zero-companion
   patterns (e.g. `lerp from 1.0 → 0.0 over my own decay window`)
   become one-liners. Con: more parameters; the lambda gets the
   field id from its capture today (it knows what it pushed).
   Recommendation: **add `ticksRemaining`**, skip `field`. The
   pre-decay snapshot of `ticksRemaining_` would let the lambda
   compute its own normalized progress without companion overhead;
   that captures a real common case (linear decay envelopes) without
   pushing into companion territory.

4. **Per-instance state for multiple drags from the same source —
   real, or hypothetical?** Velocity-drag's current design has one
   drag-source-per-target. Spawn-glow same. Spring-color same. If no
   real consumer needs the "vector keyed by field_id" companion
   shape, drop the §"Per-active-instance state" subsection and
   require companions to be flat structs in v1. Recommendation:
   **drop it for v1**; revisit when a real consumer pushes back.

## Decomposition (after architect lock-in)

The implementation breaks into:

1. **Lambda signature change + framework wiring** (this PR's
   follow-up). Updates `LambdaModifier::fn_`, `pushLambda`,
   resolve-lambda system, Lua binding thunk, and tests. No new
   semantics.
2. **Velocity-drag migration** (separate PR). Adds
   `C_VelocityDragState`, rewrites `system_velocity_drag.hpp` as a
   modifier producer, deletes the old hover/blend bookkeeping.
3. **Spawn/glow migration** (separate PR). Same shape as drag.
4. **Spring-color migration** (separate PR). Spring physics in the
   companion component.

Steps 2-4 can run in parallel after step 1 lands.

## Out of scope for this proposal

- Composing multiple stateful lambdas on the same field (already
  handled by the resolver's existing push-order semantics — each
  lambda fires in turn on the latest value).
- Cross-frame lambda *replacement* (push a new lambda that overrides
  an old one). Today's `removeBySource` + push pattern handles this;
  no new mechanism needed.
- Lambda state on *non-source* entities (e.g. drag target, not drag
  source). Caller can add the companion to whichever entity it wants
  and capture that id in the lambda — the framework only threads
  `source` because `source` is the modifier's own attribution; other
  ids are caller-managed.
