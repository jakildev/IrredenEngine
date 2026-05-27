# Lua physics bindings — surface inventory and proposed API

**Status:** Research/design — no implementation ticket filed yet. Companion
to T-193 (Lua input & command bindings) and T-196 (Lua binding
automation), both sibling tasks under the lua-game-foundation epic
(T-193..T-196).

**Decision driver:** the engine has no general-purpose physics module
(no Bullet, no Box2D). What it does have is a small, opinionated set of
ECS-driven movement, collision, hover, and picking primitives. The
question this note answers is: *which of those should be Lua-callable
free functions, and which already work purely through Lua-bound
component data?*

**Problem owner:** `engine/script/` (binding surface) +
`engine/prefabs/irreden/update/` (collision/movement systems) +
`engine/prefabs/irreden/render/picking.hpp` (CPU raycast).

---

## TL;DR — the recommendation in one paragraph

The engine's "physics" surface is mostly already Lua-reachable via the
existing component bindings (velocity, gravity, collider, contact
event, collision layer, hitbox). What's missing is the small set of
**free functions** that drive those components or live alongside them:
the mouse→world projection that the editor's picking uses, a CPU
SDF-ray cast, the GPU entity-id-at-cursor read, AABB overlap as a
direct query (currently only exposed through the collision system),
and the four `IRMath::` ballistic helpers. Bind those as
`IRPhysics.*` (raycasts + AABB overlap), `IRRender.mouseWorldAtIsoDepth /
entityAtCursor` (the mouse helpers belong to the renderer namespace),
and `IRMath.impulseForHeight / heightForImpulse / flightTimeForHeight /
isTunnelingSafe` (ballistic math fits next to the other IRMath helpers).
That's it for v1. Defer voxel-pool point queries, broadphase
generalization beyond AABB, and any general-purpose physics solver —
none exist on the C++ side and none of the current creations need
them.

---

## Engine "physics" surface — what actually exists

There is no `engine/physics/` module. The surface is spread across
five concrete locations:

### 1. Integration (`engine/prefabs/irreden/update/`)

Linear motion built directly on top of `C_Position3D`. Each is a
pure ECS system; there is no integrator class.

- `C_Velocity3D` (`component_velocity_3d.hpp:11-29`) — `vec3` velocity
  in **blocks/second** (not pixels). Already has a `_lua.hpp` binding.
- `C_VelocityDrag` (`component_velocity_drag.hpp`) — drag, hover
  damping, post-hover reset.
- `C_Gravity3D` — direction + magnitude.
- `C_Acceleration3D` — accumulates into velocity.
- `VELOCITY_3D` system (`system_velocity.hpp`) — `position +=
  velocity * deltaTime(UPDATE)`.
- `GRAVITY_3D`, `ACCELERATION_3D`, `VELOCITY_DRAG` — corresponding
  per-component integrators. Canonical UPDATE pipeline order is
  `GRAVITY_3D → VELOCITY_DRAG → VELOCITY_3D` (see
  `engine/prefabs/irreden/update/CLAUDE.md`).

**Lua reachability today:** add `C_Velocity3D` to a creation's
`lua_component_pack.hpp`, drive it from Lua, register the integrator
systems via `IRSystem.SystemName.VELOCITY_3D` / `GRAVITY_3D` /
`VELOCITY_DRAG`. All three system enum entries are already bound at
`lua_pipeline_bindings.hpp:90-104`. **Nothing missing here.**

### 2. AABB collision (`engine/prefabs/irreden/update/`)

A single iso-3D AABB pair test, gated by a layer mask, producing
contact events.

- `C_ColliderIso3DAABB` (`component_collider_iso3d_aabb.hpp`) —
  `halfExtents_` + `centerOffset_` (entity-local AABB around the
  visual origin). Already `_lua.hpp`-bound.
- `C_CollisionLayer` (`component_collision_layer.hpp`) — `layer_`,
  `collidesWithMask_`, `isSolid_` + a `canCollideWith()` self-only
  helper. Already `_lua.hpp`-bound. Layer enum values are
  C++-only today (`COLLISION_LAYER_DEFAULT / NOTE_BLOCK /
  NOTE_PLATFORM / PARTICLE`).
- `C_ContactEvent` (`component_contact_event.hpp`) —
  `entered_/stayed_/exited_/touching_/wasTouching_` flags +
  `otherEntity_` + `normal_`. Already `_lua.hpp`-bound (empty ctor
  only).
- `COLLISION_NOTE_PLATFORM` system
  (`system_collision_note_platform.hpp`) — O(N²) all-pairs scan over
  every `C_PositionGlobal3D + C_ColliderIso3DAABB + C_CollisionLayer +
  C_ContactEvent` entity, with an iso-depth broadphase
  (`detail::broadPhaseIsoDepth`) and an AABB narrow phase
  (`detail::narrowPhaseAabb`).
- `COLLISION_EVENT_CLEAR` system
  (`system_collision_event_clear.hpp`) — clears
  `entered_/stayed_/exited_/touching_/otherEntity_/normal_` and
  rolls `wasTouching_` forward. Must run early-UPDATE so the next
  collision tick re-populates from clean slate.

**Lua reachability today:** every component bound; both systems bound
via `IRSystem.SystemName` (`COLLISION_NOTE_PLATFORM` line 99,
`COLLISION_EVENT_CLEAR` line 98). A Lua-only consumer can already
read `entered_` / `otherEntity_` / `normal_` and dispatch from Lua
inside a Lua-defined system body. **Missing:** the underlying
`overlaps1D` / `narrowPhaseAabb` helpers as direct Lua queries
(useful for ad-hoc "does point P overlap this entity's collider"
without going through the per-frame all-pairs scan).

**Caveat:** the collision system's name is a historical artifact.
`COLLISION_NOTE_PLATFORM` was generalised to any entity with the
required component bundle; only the enum name still references the
midi origin. Worth renaming to `COLLISION_AABB_ISO3D` as part of a
follow-up — out of scope for this design note.

### 3. Hitbox + hover (`engine/prefabs/irreden/input/`,
`engine/prefabs/irreden/render/`)

Mouse-driven point-vs-rectangle tests with callback dispatch.

- `C_HitBox2D` (`component_hitbox_2d.hpp`) — half-extents in
  **world space (camera-transformed iso)** + `hovered_` flag.
- `C_HitBox2DGui` (`component_hitbox_2d_gui.hpp`) — full
  width/height in **GUI-canvas-trixel coordinates** (top-left origin)
  + `hovered_` flag.
- `HITBOX_MOUSE_TEST` (`system_hitbox_mouse_test.hpp`) and
  `HITBOX_MOUSE_TEST_GUI` (`system_hitbox_mouse_test_gui.hpp`) —
  INPUT-pipeline systems that stamp `hovered_` each frame, caching
  camera pos/zoom at `beginTick`.
- `SYSTEM_ENTITY_HOVER_DETECT`
  (`system_entity_hover_detect.hpp`) — dispatches
  `onHovered` / `onUnhovered` / `onClicked` / `onRightClick`
  handlers. The handler table at
  `IRSystem::getEntityEventHandlers()` stores
  `sol::protected_function` and fires Lua-style with handler-id
  bookkeeping — but the **C++ entry points
  `addOnHovered` / `addOnClicked` / etc. are not bound to Lua
  yet**. A creation can't subscribe without dropping to a custom
  binding lambda.

**Lua reachability today:** none of the hover surface is bound to
Lua. The system fires correctly, but registration is C++-only.
The two hitbox component types also lack `_lua.hpp` siblings.
**Missing:** `_lua.hpp` for both hitbox component types so
configuration (size, position) is settable from Lua, and a stable
Lua spelling for the handler-registry entry points
(`IRInput.registerHoverHandler(entity, { onHovered = fn, ... })`).

### 4. CPU raycast + mouse-to-world (`engine/prefabs/irreden/render/picking.hpp`)

Editor-side cursor → world ray that walks SDF shapes.

- `IRPrefab::Picking::castVoxelRay(excludeEntity = kNullEntity)
  → optional<RayHit>` (`picking.hpp:123-157`) — walks
  `IRRender::mouseWorldPos3DAtIsoDepth` along the canvas iso-depth
  axis at `kPickingDepthStep = 0.5f` increments, evaluates the SDF
  of every visible `C_ShapeDescriptor` at each step, returns the
  first surface hit (`SDF::evaluate <= kSurfaceThreshold`).
- `RayHit{entity_, worldHitPos_, voxelPos_}` (`picking.hpp:40-44`).
- Underlying primitives:
  - `IRRender::mouseWorldPos3DAtIsoDepth(canvasIsoDepth)
    → vec3` (`ir_render.hpp:209`) — the screen → world inverse.
    Composes `R_z(-rasterYaw) · isoPixelToPos3D · R2D(-residualYaw) ·
    screen`. Yaw-aware so `castVoxelRay` is consistent at any
    `visualYaw`.
  - `IRRender::mousePosition2DIsoWorldRender()` (`ir_render.hpp:175`),
    `IRRender::mouseTrixelPositionWorld()` (`ir_render.hpp:180`).
  - `IRRender::getEntityIdAtMouseTrixel()`
    (`ir_render.hpp:213`) — **GPU alternative**. Reads the
    persistent-mapped `HoveredEntityIdBuffer` written by
    `f_trixel_to_framebuffer`. O(1) but lags one frame and yields the
    entity only (no surface hit position).
  - `IRMath::SDF::evaluate(localPos, type, params)` (`math/sdf.hpp:101`).
  - `IRMath::SDF::boundingRadius / boundingHalf` (`math/sdf.hpp:127-161`).

**Lua reachability today:** **none**. No picking module surface is
exposed; the picking primitive lives in `engine/prefabs/` so it can't
be reached from a creation's Lua bindings without a binding layer.
The mouse-position helpers on `IRRender::` are not bound either.

**Used internally:** the editor's `VOXEL_PICKING` system
(`system_voxel_picking.hpp`) and the `GIZMO_HOVER` system
(`system_gizmo_hover.hpp`) both consume these. Any Lua-driven editor
or interactive demo would need the same surface.

### 5. IRMath ballistic helpers (`engine/math/include/irreden/math/physics.hpp`)

Four pure functions for symmetric ballistic arcs under constant
gravity. **No Lua binding today** — `IRMath` in Lua exposes only
fract / clamp01 / lerp / lerpByte / hsvToRgb / layout helpers
(`engine/script/src/lua_script.cpp:156-272`).

- `IRMath::impulseForHeight(gravity, height) → float` (line 11).
- `IRMath::flightTimeForHeight(gravity, height) → float` (line 18).
- `IRMath::heightForImpulse(gravity, impulseSpeed) → float` (line 25).
- `IRMath::isTunnelingSafe(maxVelocity, dt, thicknessA, thicknessB)
  → bool` (line 39).

### 6. Specialty: spring platforms, rhythmic launch, contact-driven effects

A handful of update systems built on top of the
collider-or-velocity primitives. These are **gameplay-specific** and
worth listing separately because they're not "physics" in the
general sense — they're scripted behaviors keyed off contact
events.

- `SPRING_PLATFORM` (`system_spring_platform.hpp`) — five-state
  spring (IDLE / CATCHING / LOCKED / ANTICIPATING / LAUNCHING /
  REBOUNDING) driven by `C_ContactEvent`. Calls
  `getComponentOptional<C_Velocity3D>(contact.otherEntity_)` per
  contact — the documented `cpp-ecs.md` foreign-entity-lookup
  violation. Tracked as a known deviation; future collision systems
  should batch foreign entities into the event component instead.
- `RHYTHMIC_LAUNCH` (`system_rhythmic_launch.hpp`) — timed launch
  trigger keyed off a spring entity.
- `CONTACT_NOTE_BURST`, `CONTACT_TRIGGER_GLOW`,
  `CONTACT_MIDI_TRIGGER` — fire one-shot side effects on contact.

**Lua reachability today:** these systems are all bound in
`lua_pipeline_bindings.hpp`; the
underlying `C_SpringPlatform` component has a `_lua.hpp` binding
(`component_spring_platform_lua.hpp`). They are a useful set to
keep working but they don't define new Lua surface area — they're
parameterised through their existing component data.

---

## Cross-cutting: what already works from Lua

To avoid recommending what already exists, the table below maps each
surface bullet above to its current Lua reachability. The
"Reachable today?" column counts only first-class spellings — if a
caller has to drop to `IRScript::lua().raw_state()` or hand-write a
sol2 lambda, that's a "no".

| Surface | Component | System | Reachable today? |
|---|---|---|---|
| Velocity integration | `C_Velocity3D` (bound) | `VELOCITY_3D`, `ACCELERATION_3D`, `GRAVITY_3D`, `VELOCITY_DRAG` (all bound) | **Yes** |
| AABB collision | `C_ColliderIso3DAABB` (bound), `C_CollisionLayer` (bound), `C_ContactEvent` (bound) | `COLLISION_NOTE_PLATFORM`, `COLLISION_EVENT_CLEAR` (both bound) | **Yes** for the per-frame all-pairs path. **No** for "did this point/ray overlap that AABB" ad-hoc query. |
| Hover / hitbox | `C_HitBox2D`, `C_HitBox2DGui` — **no `_lua.hpp` binding** | `HITBOX_MOUSE_TEST*` (both bound) | **No.** Hitbox components have no Lua binding, and the hover-detect handler registry (`addOnHovered` / `addOnClicked` / ...) has no Lua entry point — `sol::protected_function` is the parameter type but a creation can't reach `addOnHovered` from Lua. |
| CPU raycast | n/a (free function) | n/a | **No** |
| Mouse → world | n/a (renderer free functions) | n/a | **No** |
| GPU entity-at-cursor | n/a | n/a | **No** |
| SDF math | n/a | n/a | **No** |
| Ballistic helpers | n/a | n/a | **No** |
| Spring/contact effects | `C_SpringPlatform` (bound), `C_RhythmicLaunch`, etc. | `SPRING_PLATFORM`, `RHYTHMIC_LAUNCH`, `CONTACT_*` (all bound) | **Yes** through component data; behavior is fixed in the C++ system. |

The "No" rows define the v1 binding scope.

---

## Proposed Lua API — concrete signatures

Five small surfaces. All five live in `engine/script/include/irreden/
script/` as separate `lua_<surface>_bindings.hpp` files following the
`lua_pipeline_bindings.hpp` pattern (one header per IR* namespace),
exposed by a single `bindIRPhysics()` (or per-namespace) call from
`LuaScript::bindLuaDrivenEcs`. Match the existing `IRTime` / `IRSystem`
spelling — top-level table per namespace, free functions assigned as
sol lambdas.

### 1. `IRPhysics.*` — direct query primitives

```lua
-- AABB overlap query (point vs entity, ray vs entity, AABB vs entity).
-- Reads the entity's C_PositionGlobal3D + C_ColliderIso3DAABB.
-- Returns false if the entity lacks the bundle.
IRPhysics.pointInEntity(entity, point)              -- → bool
IRPhysics.aabbOverlapsEntity(entity, center, halfExtents) -- → bool

-- SDF-shape raycast. Walks visible C_ShapeDescriptor entities along
-- the screen-cursor ray. Returns nil on miss; otherwise:
--   { entity = EntityId, worldPos = vec3, voxelPos = ivec3 }
-- Optional excludeEntity skips the editor's highlight entity.
IRPhysics.castCursorRay(excludeEntity)              -- → RayHit | nil

-- Generic point-in-shape query against an entity's SDF descriptor.
-- Returns nil if the entity has no C_ShapeDescriptor; otherwise the
-- SDF signed distance (< kSurfaceThreshold means "inside the surface").
IRPhysics.sdfDistanceTo(entity, worldPoint)         -- → float | nil

-- AABB / SDF bounding-volume queries — same primitives the collision
-- broadphase and the picker use. Useful for hand-rolled spatial logic
-- in Lua that doesn't want to wire up the whole collision pipeline.
IRPhysics.aabbOverlapsAabb(centerA, halfExtentsA, centerB, halfExtentsB) -- → bool
IRPhysics.shapeBoundingHalf(shapeType, params)      -- → vec3
IRPhysics.shapeBoundingRadius(shapeType, params)    -- → float
```

`shapeType` is an integer that lines up with `IRMath::SDF::ShapeType`
— bind that enum under `IRPhysics.ShapeType` the same way
`IRTime.UPDATE` is bound (table of integers, derived via a macro per
the `IR_BIND_*` convention). The shape enum is a renderer-side
concept; `IRRender.ShapeType` is just an alias of `IRMath::SDF::ShapeType`
so the `IRPhysics` table can reuse the same values without
re-encoding them.

### 2. `IRRender.mouseWorldAtIsoDepth`, `IRRender.entityAtCursor`

Mouse helpers live under `IRRender` (the renderer owns the
screen → world inverse). Bind a small subset, not the full set, to
keep the v1 surface bounded:

```lua
-- Mouse projected to a 3D world point at the given canvas-frame iso
-- depth. Yaw-aware. Use this to spawn entities at the cursor's
-- screen position on a known plane.
IRRender.mouseWorldAtIsoDepth(canvasIsoDepth)      -- → vec3

-- GPU entity-id under the cursor. Returns 0 (kNullEntity) if no
-- voxel/shape was rasterized there. One frame of lag — same caveat
-- as the C++ API.
IRRender.entityAtCursor()                          -- → EntityId
```

Defer the other three mouse-position variants
(`mousePosition2DIsoScreenRender`, `mousePosition2DIsoWorldRender`,
`mouseTrixelPositionWorld`) until a Lua caller actually needs them —
they exist because the C++ side has render/update pipeline rate
asymmetries that Lua callers don't observe in the same way.

### 3. `IRInput.registerHoverHandler` — first-class hover callback registration

The hover-detect system already accepts Lua callbacks via
`sol::protected_function` (`system_entity_hover_detect.hpp`); the
registration entry point is what's missing from the Lua surface. The
shape that fits the rest of the engine:

```lua
-- All callbacks optional; pass nil to skip a transition.
IRInput.registerHoverHandler(entity, {
    onHovered    = function(entity) ... end,
    onUnhovered  = function(entity) ... end,
    onClicked    = function(entity) ... end,
    onRightClicked = function(entity) ... end,
})
-- Returns a handle that can be passed back to deregister.

IRInput.deregisterHoverHandler(handle)
```

Existing C++ handler-id machinery already supports deregistration
keyed by id — the Lua surface just wraps it. This is part of T-194's
deliverable because the hover/click *callback* is the closest the
engine has to a Lua-side "physics event" surface — input + collision
both ultimately need callback dispatch into Lua.

### 4. Missing `_lua.hpp` for hitbox components

Two trivial bindings:

```cpp
// engine/prefabs/irreden/input/components/component_hitbox_2d_lua.hpp
template <> inline constexpr bool kHasLuaBinding<C_HitBox2D> = true;
template <> inline void bindLuaType<C_HitBox2D>(LuaScript &s) {
    s.registerType<
        C_HitBox2D,
        C_HitBox2D(IRMath::u8vec2),
        C_HitBox2D()>(
        "C_HitBox2D",
        "size", &C_HitBox2D::size_,
        "hovered", &C_HitBox2D::hovered_
    );
}
// (same shape for C_HitBox2DGui)
```

Add them in the same PR as `IRInput.registerHoverHandler` so the full
hover surface lights up together.

### 5. `IRMath.*` ballistic + tunneling helpers

```lua
IRMath.impulseForHeight(gravity, height)            -- → float
IRMath.flightTimeForHeight(gravity, height)         -- → float
IRMath.heightForImpulse(gravity, impulseSpeed)      -- → float
IRMath.isTunnelingSafe(maxVelocity, dt, thicknessA, thicknessB) -- → bool
```

Bind alongside the existing `IRMath.fract / lerp / clamp01 / hsvToRgb`
in `lua_script.cpp:156-272`. Four lines of new code.

---

## Prefab systems needing Lua hooks

Of the systems listed in §1–§6 above, the categorisation against the
existing pipeline-binding table (`lua_pipeline_bindings.hpp:80-162`):

| System | Bound in `IRSystem.SystemName`? | Needs Lua hook? |
|---|---|---|
| `VELOCITY_3D`, `ACCELERATION_3D`, `GRAVITY_3D`, `VELOCITY_DRAG` | Yes (90, 91, 92, 104) | No — pure C++ per-component integration; Lua just provides data. |
| `COLLISION_NOTE_PLATFORM`, `COLLISION_EVENT_CLEAR` | Yes (99, 98) | No — emits `C_ContactEvent` that Lua-defined systems can read. |
| `HITBOX_MOUSE_TEST`, `HITBOX_MOUSE_TEST_GUI` | Yes (87, 88) | No — stamps `hovered_` for the hover-detect system. |
| `ENTITY_HOVER_DETECT` | Yes (86) | **Yes** — but the hook is the callback registration (proposal §3), not the system itself. |
| `VOXEL_PICKING` | Not bound; editor-only | **No** in v1 — but a Lua editor could need it. The proposed `IRPhysics.castCursorRay` is the ad-hoc alternative that doesn't require driving the editor's selection-highlight machinery from Lua. |
| `SPRING_PLATFORM`, `RHYTHMIC_LAUNCH`, `CONTACT_*` | Yes (123, 118, 102, 103, 101) | No — behaviors are fixed in C++; Lua drives via the bound components. |
| `GIZMO_HOVER`, `GIZMO_DRAG` | Not bound; editor-only | No — F-0.5 editor surface, out of scope for game-foundation epic. |

The takeaway: **only one prefab system needs a new Lua hook**
(hover-detect's callback registration). Everything else is data-driven
through already-bound components, or out of scope for v1.

---

## What's deliberately out of scope (and why)

This list exists so a follow-up reader knows what was looked at and
explicitly deferred, not what was missed.

### Voxel-pool point queries / "is this position solid?"

The engine has no `isVoxelAt(x, y, z)` API. The voxel pool
(`C_VoxelPool`) is a packed list of voxels, not a spatial grid — point
queries would require either a spatial index (none today) or a linear
scan (O(N) over visible voxels, prohibitive for game logic). The
`castVoxelRay` path exists in `picking.hpp` and walks shapes, not
voxels — at editor zooms the visible-shape count is small, and SDF
eval is cheaper than scanning the pool. Adding a Lua-callable
"sample voxel at world pos" would require a real spatial index. Defer
until a creation needs it and we know what shape it should take.

### General-purpose physics solver (rigid body, soft body, constraints)

Not in the engine. Not on the roadmap visible from the GitHub issue queue or the
`docs/design/` directory. The spring-platform behavior is the closest
thing we have to constrained dynamics and it lives in one `System<>`
specialization keyed off contact events. A general solver is many PRs
of work and not what this design note exists to plan.

### Non-AABB collider shapes (sphere / capsule / arbitrary mesh)

`C_ColliderIso3DAABB` is the only collider component. Sphere/circle
tests would need a new collider component, a new collision system
(or extending `COLLISION_NOTE_PLATFORM` to dispatch by collider
type), and a new pair test. The existing `C_HitboxCircle`
component (`component_hitbox_circle.hpp`) exists but is not
consumed by any system — historical dead code or aspirational.
Out of scope.

### Layer enum exposure to Lua

`COLLISION_LAYER_DEFAULT / NOTE_BLOCK / NOTE_PLATFORM / PARTICLE`
are C++-side bitmask values. Lua callers today have to write the
integer literals (`layer = 1, mask = 2` etc.). A natural follow-up
is to bind these via `IR_BIND_*` like the system-name table. Trivial,
but bundled into the implementation PR rather than its own task.

### Custom callback shapes for `C_ContactEvent`

Today, a Lua-defined system iterates entities with `C_ContactEvent`
and dispatches in-Lua. This is the same pattern hover uses pre-v1.
A future `IRInput.registerContactHandler` shape (per-entity callback,
keyed by collision layer) would mirror the hover proposal — defer
until a creation has a real use case so the API shape comes from
real callers, not the design note.

### Renaming `COLLISION_NOTE_PLATFORM` → `COLLISION_AABB_ISO3D`

The system name is a midi-origin artifact; the implementation is
fully generic. Worth renaming for clarity and to make the Lua spelling
descriptive (`IRSystem.SystemName.COLLISION_AABB_ISO3D` reads better
than the current name), but it's a cross-cutting rename that touches
the enum entry, the IR_BIND_SYS line, the system definition, every
demo that registers it, and (if any) creation Lua files. Separate
task.

---

## Concrete implementation plan (follow-up tasks)

Three small PRs, in order. Each is one bounded slice; none requires
research beyond what's in this note. All [opus]-tagged because the
binding layer touches the same `engine/script/` surface as T-193
and is design-sensitive at the API-boundary.

### Follow-up A — `IRMath` ballistic helpers + hitbox component bindings

Smallest slice; lowest risk. Establishes the pattern for the two
remaining PRs.

- Add 4 lambdas under `m_lua["IRMath"]` in `lua_script.cpp:272-ish`
  for the ballistic helpers.
- Add `component_hitbox_2d_lua.hpp` and
  `component_hitbox_2d_gui_lua.hpp` mirroring the existing trivial
  bindings.
- Test: small Lua snippet in `test/script/lua_script_test.cpp`
  confirming each new spelling resolves and returns the right type.

**Acceptance:** `fleet-build` clean on linux-debug + macos-debug;
`test/script` passes; `m_lua["IRMath"]["impulseForHeight"](9.8, 10)`
returns a finite float matching the C++ result; the two new component
types can be created and round-tripped through
`registerTypesFromTraits<>`.

### Follow-up B — `IRPhysics.*` namespace + `IRRender.mouseWorldAtIsoDepth` / `entityAtCursor`

The bulk of the binding work.

- New `lua_physics_bindings.hpp` exposing `IRPhysics.pointInEntity`,
  `aabbOverlapsEntity`, `castCursorRay`, `sdfDistanceTo`,
  `aabbOverlapsAabb`, `shapeBoundingHalf`, `shapeBoundingRadius`, and
  the `IRPhysics.ShapeType` table of integers.
- Two lambdas under `m_lua["IRRender"]` (the table doesn't exist yet;
  create it the same way `IRTime` is created in `bindIRTimeEvents`):
  `mouseWorldAtIsoDepth(canvasIsoDepth)` and `entityAtCursor()`.
- `bindIRPhysics()` member function on `LuaScript`, called by
  `bindLuaDrivenEcs()` so the surface lights up uniformly when a
  creation opts in.
- Lua-side tests covering the SDF distance math + ray cast in a
  small fixture.
- Update `engine/script/CLAUDE.md` with a new section mirroring
  "Modifier framework (`IRModifier.*`)".

**Acceptance:** the test fixture's `castCursorRay` round-trips
through an `IRPrefab::Picking::castVoxelRay` call and returns the
same `entity / worldPos / voxelPos` triple; `pointInEntity` agrees
with `narrowPhaseAabb` on an N=20 entity sample. `fleet-build`
clean on both backends.

### Follow-up C — `IRInput.registerHoverHandler` / `deregisterHoverHandler`

- New `IRInput` Lua table populated in `bindLuaDrivenEcs` (or
  earlier — match how `IRSystem` is set up).
- Adapter from the existing hover-handler registry to a Lua-friendly
  table-of-callbacks shape (one Lua call, four optional fields).
- Symmetric deregistration keyed by the returned handle.
- Document the lifetime contract (`sol::protected_function` must
  outlive the registration) in `engine/script/CLAUDE.md`.

**Acceptance:** a Lua-only fixture creates an entity with
`C_HitBox2D + C_Position3D`, registers all four callbacks, simulates
hover transitions, and observes each callback fire exactly once per
transition (mirrors the existing C++ hover-detect test). Confirm the
existing hover-detect test in `test/ecs/` still passes unchanged.

---

## Open questions for the architect

1. **Where does `IRPhysics` live in the source tree?** Two options:
   keep it as a `lua_physics_bindings.hpp` under `engine/script/include/`
   (the binding-layer mirror of `engine/script/`'s other bindings), or
   create a thin `engine/physics/` module with the AABB-helpers and
   SDF-helpers as C++-side free functions, then bind those. The
   latter is more orthodox if/when more physics surface is added;
   the former is enough for v1. Recommend the former for now; revisit
   if Follow-up B grows beyond ~200 lines of binding code.

2. **Should `castCursorRay` accept an alternative "ray from a world
   point in a given direction" overload?** Today it's cursor-only
   (driven by `IRRender::mouseWorldPos3DAtIsoDepth` internally). A
   `castRay(origin, direction, maxDistance)` form is the natural next
   step for non-mouse use cases (AI line-of-sight, projectile
   pre-flight checks). Defer to a follow-up — adding it now is
   speculative.

3. **The hover handler returns `void` today. Should the Lua-side
   `onClicked` signal "handled, don't propagate to lower-priority
   tiers"?** Hover-detect currently resolves through `GUI > world >
   trixel` priority and stops at the first hit; an `onClicked`
   handler that returned `true` could opt into "I consumed this
   click, lower tiers don't fire." Cleaner UX but adds API
   surface — defer until a creation needs it.

---

## Acceptance — measured against T-194's `Notes` field

The task notes flagged: *"Likely smaller scope than T-193. If the
engine has minimal physics, this collapses to 'bind raycast +
voxel intersection from Lua.'"*

The actual collapse goes one step further: the engine has **no voxel
intersection** in the sense the note imagined (no `sampleVoxel(p)`
API), but it has shape-based raycast through `picking.hpp` and AABB
overlap through the collision system's detail helpers — both
adaptable to Lua via thin wrappers. So the v1 binding is "raycast
against SDF shapes + AABB overlap + a couple of mouse-to-world
helpers + the ballistic primitives." Three small follow-up PRs,
likely 200-400 lines combined.

Implementation deferred to Follow-ups A–C above. This note is the
research deliverable for T-194.
