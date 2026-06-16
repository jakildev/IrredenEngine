# Plan: Lua-facing collision/trigger overlap events

- **Issue:** #1817
- **Model:** sonnet
- **Date:** 2026-06-15

## Scope
Surface the engine's **existing** AABB overlap detection to Lua as overlap
events. A creation registers a Lua handler keyed by a **collision-layer pair**;
the handler fires on **enter** (and **exit**) of any two entities on those layers
and receives **both entity ids**. No physics response/solver — overlap
notification only. Lua then dispatches on the source entity's tag components
(the engine-native source-kind pattern, `irreden/docs/effects.md`) — the engine
just delivers `(entityA, entityB)`.

## One task or a stack? — ONE sonnet PR (decision)
The detection already exists (`COLLISION_NOTE_PLATFORM` produces overlaps;
`C_CollisionLayer` is the tag mechanism; `C_ContactEvent` already computes
enter/stay/exit per entity). The new work is a thin vertical slice: (1) make the
detector emit a **batched contact-pair vector**, (2) a new dispatch system that
diffs pair-overlap frame-to-frame and invokes registered Lua handlers, (3) shared
Lua bindings. It is cohesive same-PR work and the design forks below are resolved
here, so it does not need decomposition. **If the implementer finds it exceeds one
PR**, the only sanctioned fault line is `[batched-pair emit + dispatch system]` →
`[Lua bindings + demo]` filed as a *stacked* follow-up (`**Blocked by:** #1817`),
never flat siblings on the same new files.

## Key design decisions (resolved — execute these)

### D1 — Layer-pair-keyed GLOBAL handler registry (NOT per-entity)
Register handlers by **collision-layer pair**, held in the dispatch system's
state, NOT in an ECS component. Rationale:
- Storing a `sol::protected_function` in a component fights the binding-trait /
  POD-data component model and creates a per-entity lifetime problem (dangling
  handles on entity death). Layer-keyed global registration **sidesteps lifetime
  entirely** — handlers live for the session; entity death simply yields no
  further contacts. This is what keeps the task genuinely sonnet-class.
- Matches the existing tag mechanism (`C_CollisionLayer`, bitmask layers) and the
  issue's "two **tagged** entities" language.
- Follows the established C++-owns-`sol::protected_function` pattern, NOT a
  component field — model on `lua_command_bindings.hpp:297-330` (store
  `sol::protected_function`, invoke with `protected_function_result` + error trap)
  and `lua_script.hpp:255-264` (per-Lua-callback `shared_ptr<sol::protected_function>`).

### D2 — Batched contact-pair vector (follow `.claude/rules/cpp-ecs.md`)
The issue mandates the batched-vector contact pattern, not per-pair `getComponent`.
`COLLISION_NOTE_PLATFORM` already finds each overlapping pair in its broad+narrow
scan (`detail::markContact`, `system_collision_note_platform.hpp:78-88`). Have it
also append each overlap to a frame-scoped vector of:
```cpp
struct ContactPair {                 // co-locate near C_ContactEvent
    IREntity::EntityId a_, b_;       // a_ < b_ (canonical order)
    uint32_t layerA_, layerB_;       // each entity's C_CollisionLayer.layer_, matching a_/b_
    vec3 normal_;
};
```
Because the detector has **both** colliders + both `C_CollisionLayer`s in hand
during the scan, it stamps both layers into the pair — so the dispatch system
needs **zero `getComponent`** on the foreign entity (this is the whole point of
the rule; the anti-pattern to avoid is `system_spring_platform.hpp:53-60`).

Share the vector via the engine's idiomatic cross-system mechanism: a **singleton
component** holding `std::vector<ContactPair>` (producer writes, consumer reads),
cleared at frame boundary in `COLLISION_EVENT_CLEAR`. If the codebase has no
singleton-component precedent the implementer can confirm, fall back to holding
the vector in `System<COLLISION_NOTE_PLATFORM>` state with a const accessor the
dispatch system reads — confirm the chosen mechanism against existing
producer→consumer systems before coding.

### D3 — Enter/exit derived at the PAIR level by the dispatch system
The existing `C_ContactEvent` enter/exit flags are **per-entity** (one contact per
entity per frame, `component_contact_event.hpp:11-28`) — insufficient for accurate
pair-level enter/exit when an entity touches several others. So the dispatch
system owns the enter/exit state machine at pair granularity:
- Keep `std::unordered_set<PairKey>` of pairs overlapping **last** frame
  (`PairKey` = packed `(min(a,b), max(a,b))`).
- Each frame, build the current overlapping-pair set from the D2 vector.
- `current \ previous` → **ENTER** → invoke `onOverlapEnter` handlers for that
  layer pair. `previous \ current` → **EXIT** → invoke `onOverlapExit`.
- Swap previous := current.

This lives entirely in dispatch-system state — **no component changes to
`C_ContactEvent`**, and existing consumers (`system_spring_platform`) are
untouched.

### D4 — Lua API surface (enum-as-table, `.claude/rules/cpp-lua-enums.md`)
Add a shared `IRCollision` Lua table (new `lua_collision_bindings.hpp`, wired into
`bindLuaDrivenEcs()` like `lua_render_bindings.hpp`):
```lua
IRCollision.Layer = { DEFAULT=1, NOTE_BLOCK=2, NOTE_PLATFORM=4, PARTICLE=8 }  -- from C_CollisionLayer enum via #name macro
IRCollision.onOverlapEnter(layerA, layerB, function(entA, entB) ... end)
IRCollision.onOverlapExit (layerA, layerB, function(entA, entB) ... end)
```
- `Layer` table is generated from the `COLLISION_LAYER_*` enum with the
  `#define IR_BIND(name) t[#name]=...` one-liner macro (never string checks);
  Lua may also pass raw integer layer bits, so creations not limited to the four
  named layers still work.
- Handler keyed by the **unordered** layer pair, but the engine **orders the two
  entity ids to match the registered arg order**: `onOverlapEnter(L1, L2, fn)` →
  `fn(entityOnL1, entityOnL2)`. For same-layer overlaps (`L1==L2`) the order is
  `(a_, b_)` (canonical) — document this.

## Approach (ordered)
1. **`ContactPair` type + batched vector** — define `ContactPair` near
   `component_contact_event.hpp`; add the singleton/state container (D2) and clear
   it in `system_collision_event_clear.hpp`.
2. **Emit pairs from the detector** — in
   `system_collision_note_platform.hpp:130-190`, where an overlap is confirmed,
   append a canonical-ordered `ContactPair` (both layers stamped) to the D2 vector.
   Additive only; leave per-entity `C_ContactEvent` writes intact.
3. **New dispatch system** — `SYSTEM_DISPATCH_LUA_OVERLAP` (add to `SystemName`
   enum, `ir_system_types.hpp`, near `COLLISION_*`). State holds: the handler
   registry (`map<LayerPairKey, vector<sol::protected_function>>` for enter and for
   exit) and the previous-frame pair set (D3). Tick: diff pairs → invoke matching
   handlers with error-trapped `protected_function_result`. Register it in the
   update pipeline **after** `COLLISION_NOTE_PLATFORM` and **before**
   `COLLISION_EVENT_CLEAR` (so the pair vector is still populated).
4. **Lua bindings** — `lua_collision_bindings.hpp` (`namespace IRScript::detail`,
   header-only `bindCollisionEvents(LuaScript&)`), wired into `bindLuaDrivenEcs()`
   in `lua_script.cpp`. `onOverlapEnter/Exit` store the `sol::protected_function`
   into the dispatch system's registry (reach the system via the SystemName
   accessor, mirroring how other bindings reach engine state).
5. **Demo** — extend or add a minimal Lua creation proving
   projectile-vs-enemy AND pickup-vs-player overlap handlers fire from Lua
   (acceptance criteria 2). Reuse an existing scripted demo if one fits.
6. **Docs** — record the new `IRCollision` surface in `engine/script/CLAUDE.md`
   (Lua API section) and note the batched-pair emit in the relevant
   `engine/prefabs/.../CLAUDE.md`; close the `irreden/docs/dev/lua-first-patterns.md`
   §GUI/collision gap pointer if applicable (game-repo doc — leave a cross-ref, do
   not edit the game repo from the engine PR).

## Risks / forks flagged for the implementer
- **Cross-system data sharing mechanism (D2)** — confirm singleton-component vs.
  system-state-accessor against the codebase before coding; both are viable, pick
  the one with precedent.
- **`COLLISION_NOTE_PLATFORM` is misnamed** (music-demo origin) but IS the general
  AABB overlap detector. Do NOT rename in this PR (out of scope); a rename is its
  own mechanical task.
- **Layer vocabulary** — the four named layers are music-demo-centric; arcade
  gameplay (player/enemy/projectile/pickup/wall) will use raw integer bits or new
  named layers. Expose raw-int support (D4) so the engine stays generic; adding
  game-specific named layers is the game's job, not this engine PR.
- **Same-frame multi-contact** — D3's pair-level set handles an entity touching
  several others correctly; do not regress to the per-entity single-contact model.
- **If lifetime pressure appears** (e.g. a real need for per-entity handlers),
  that's an opus-tier redesign — escalate per role step 8a rather than stuffing
  `sol` handles into components.

## Acceptance-criteria mapping
- *Lua handler fires on overlap of two tagged entities, receives both ids* →
  D1 + D4 (`onOverlapEnter(layerA, layerB, fn(entA, entB))`).
- *projectile-vs-enemy and pickup-vs-player both work from Lua* → step 5 demo.
- *No per-pair `getComponent` in a tick; batched-vector contact pattern* →
  D2 (layers stamped into the pair; dispatch reads the vector, never reaches the
  foreign entity).
