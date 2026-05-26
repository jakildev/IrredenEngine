---
name: increase-complexity
description: >-
  Auto-grow a demo or creation by scanning available engine systems and
  proposing/applying additive changes to make it more visually impressive.
  Use when the user says "increase complexity", "make more impressive",
  "add more systems", "make number go up", "make it fancier", "add more to
  this demo", or "auto-grow this demo". Also use when the user specifies a
  concrete enhancement: "add particles", "add animation", "add lighting",
  "add more entities". Never removes existing features — additive only.
---

# increase-complexity

Makes a target creation more visually complex by scanning the engine's
prefab system catalog, comparing against what the creation already uses,
and proposing concrete additive changes — new entity types, higher entity
counts, animations, particles, lighting effects, or interactive behaviors.

**Additive only.** Never remove or restructure existing systems or entities.
Every change must be a pure addition that compiles and runs cleanly without
touching lines not in scope.

---

## Step 0 — Identify the target creation

The user may pass a creation name as an argument (e.g.
`/increase-complexity shape_debug`). If not specified, infer from context:

1. Is the current working directory inside `creations/demos/<name>/` or
   `creations/editors/<name>/`? If so, that's the target.
2. Does the current git branch contain a creation name? If so, prefer that.
3. If neither, ask the user: "Which creation should I grow?" and offer the
   `creations/demos/` list.

If the target is inside a gitignored directory under `creations/`, read that
directory's `CLAUDE.md` first — it may restrict what this skill is allowed
to touch.

---

## Step 1 — Inventory what the creation currently does

Read all of:
- `creations/demos/<name>/main.cpp` (or `main_lua.cpp`) — the C++ entry
  point; focus on the system-registration block and `initEntities()`
- `creations/demos/<name>/main.lua` (if Lua-driven) — Lua systems table
  and entity creation calls

Extract:
- **Registered systems** — collect every `SystemName` enum value passed to
  `IRSystem::registerPipeline()` or listed in the Lua `systems = {}` table.
  This is the "already-used" set.
- **Entity shapes** — note the rough entity count and types (grid of SDF
  shapes? single voxel set? sprites?).
- **Existing behaviors** — any motion, animation, color change, or
  interactivity already present.

Summarize in one sentence: "This demo renders N shape entities with systems
X, Y, Z; no motion or animation systems."

---

## Step 2 — Scan available systems

Read the system enum file:
`engine/system/include/irreden/system/ir_system_types.hpp`

Use the comment group headers in the enum file to build a categorized list.
Then diff that against the "already-used" set from Step 1.

For each candidate system in the "unused" set, look up its header at:
`engine/prefabs/irreden/<domain>/systems/system_<snake_name>.hpp`
(e.g. `SystemName::PERIODIC_IDLE` → read
`engine/prefabs/irreden/update/systems/system_periodic_idle.hpp`)

From the header, extract:
- What components it requires on entities
- Whether `create()` takes constructor arguments
- The visual / behavioral effect in one line

Prioritize candidates by visual impact in this order:
1. **Motion / animation** (PERIODIC_IDLE, PERIODIC_IDLE_POSITION_OFFSET,
   AUTO_YAW_ROTATE, VELOCITY_3D, GRAVITY_3D, ANIMATION_COLOR) — highest
   audience impact per line of code
2. **Particles** (PARTICLE_SPAWNER, SPAWN_GLOW,
   RENDER_STATELESS_PARTICLES_TO_TRIXEL) — visually striking
3. **Lighting changes** (extra light sources, fog variation) — easy win
4. **More entities** — scale up the entity count in `initEntities()`
5. **Interactivity** (HITBOX_MOUSE_TEST, SPRING_PLATFORM) — engagement

Skip systems that require architectural prerequisites not present in the
demo (e.g. `MIDI_*` systems when the demo has no MIDI device setup,
`CHUNK_RESIDENCY_*` when the demo is single-chunk, GPU-compute systems when
the demo doesn't have a matching SSBO pipeline).

---

## Step 3 — Propose changes

Present a short numbered menu to the user (1–4 options, each with a
one-line description and estimated LoC change):

```
Proposals for <demo-name>:
  1. Idle bounce — register PERIODIC_IDLE + PERIODIC_IDLE_POSITION_OFFSET
     on existing shape entities; shapes bob vertically at a sine frequency.
     ~6 lines.
  2. Auto-rotation — register AUTO_YAW_ROTATE on the camera at
     0.003 rad/frame. 1 line.
  3. Spawn glow particles — register SPAWN_GLOW; add C_SpawnGlow to 4
     corner entities (also needs C_VoxelSetNew already on shape entities);
     small emitter halos appear. ~8 lines.
  4. Double entity count — duplicate the N×N grid to 2N×2N in
     initEntities(). 3 lines.
  all — apply all of the above.
```

If the user already specified a concrete request ("add particles"), skip the
menu and jump directly to Step 4 with that change.

If the user says "all" or "just do it", apply all proposals without asking.

---

## Step 4 — Apply the changes

For each accepted proposal, make ONLY additive edits:

### 4a — C++ system registration

In `main.cpp`, find the existing `registerPipeline()` block for the relevant
pipeline stage (UPDATE, RENDER, INPUT). Add the new system call immediately
after the last existing call in that group. Match indentation and style
exactly — do not reformat surrounding lines.

Include the system header near the other `// SYSTEMS` includes at the top of
the file.

Example (idle-bob — two systems needed, both in UPDATE):
```cpp
// after existing UPDATE pipeline registrations:
IRSystem::registerPipeline(
    IRTime::Events::UPDATE,
    {IRSystem::createSystem<IRSystem::PERIODIC_IDLE>(),
     IRSystem::createSystem<IRSystem::PERIODIC_IDLE_POSITION_OFFSET>()}
);
```

Most systems use `IRSystem::createSystem<IRSystem::NAME>()`; systems with
parameterized setup (e.g. `AUTO_YAW_ROTATE`) define their own
`System<N>::create(arg)` — check the system header for the exact signature.

### 4b — Entity component additions

If a new system requires a component on existing entities (e.g.
`PERIODIC_IDLE` + `PERIODIC_IDLE_POSITION_OFFSET` need `C_PeriodicIdle` and
`C_Modifiers` on the entity), add those components to the existing
`createEntity()` call(s) in `initEntities()`.
Do not create a new entity just to carry a component that belongs on an
existing one.

Include the component header near the other `// COMPONENTS` includes.

### 4c — New entity types

When a genuinely new entity type is needed (a particle emitter, a glow
source, an audio trigger), add a new `createEntity()` call in `initEntities()`
after the existing entity creation block. Give it a comment line explaining
what it does.

### 4d — Scale up entity count

When duplicating entities to "make number go up", find the loop or array in
`initEntities()` that creates the grid. Extend the loop bounds or add a
second nested loop call. Aim for a 2–4× increase unless the user specifies
otherwise.

### 4e — Lua entry points

In `main.lua`, add new system names to the `systems = {}` table and new
entity tables to the creation table. Match the style of existing entries.
Prefer `IRSystem.SystemName.X` (enum integer binding) over string names.

---

## Step 5 — Build and run

```
fleet-build --target <ExecutableName>
```

If the build fails:
1. Read the full error message.
2. Fix the minimal set of lines that caused the error (missing include,
   wrong argument type, missing component, wrong pipeline stage).
3. Rebuild.
4. Repeat up to 3 times. If it still fails, stop and report the error —
   don't guess at bigger refactors.

Once it builds:
```
fleet-run --timeout 15 <ExecutableName>
```

Watch for crashes or error output. If the demo launches and runs for 15
seconds without crashing, it's done.

---

## Step 6 — Report

After a successful run, print:

- **Added**: one-line summary of what was added (systems, entity count delta)
- **Before / after**: entity count or system count delta if meaningful
- **Build command**: `fleet-build --target <Name> && fleet-run <Name>`
- **Deferred**: any proposals that were skipped and why (missing prerequisite,
  user declined, build failure)

If the demo supports `--auto-screenshot`, offer to capture a before/after
pair: `fleet-run <Name> --auto-screenshot 10`. Don't capture automatically
— only when the user asks.

---

## Boundaries and non-goals

- **This skill does not create new demos.** Use `/create-creation` for that.
- **This skill does not create new engine systems.** Use `/ecs-prefab-creator`
  when the right system doesn't exist yet.
- **This skill does not optimize.** If the demo frame rate drops after the
  additions, note it in the report and let the user decide whether to invoke
  `/optimize`.
- **Private creations**: if the target is in a gitignored directory, do not
  commit anything — the user's creation has its own git. Report what was
  added and stop before any git operation.

---

## Quick reference: high-value system–component pairs

These additions have been validated against the engine system enum and headers.
System names are exact `SystemName` enum values; verify current component names
in the system header before use — the table is a starting-point cache only.

| Goal | System(s) to register | Component(s) to add to entity |
|---|---|---|
| Idle bounce (vertical bob) | `PERIODIC_IDLE`, `PERIODIC_IDLE_POSITION_OFFSET` | `C_PeriodicIdle`, `C_Modifiers` |
| Camera auto-rotation | `AUTO_YAW_ROTATE` | *(none — iterates existing `C_Camera` entity; `System<AUTO_YAW_ROTATE>::create(rad)`)* |
| Velocity-driven motion | `VELOCITY_3D`, `PROPAGATE_TRANSFORM` | `C_Velocity3D` |
| Gravity | `GRAVITY_3D` | `C_Gravity3D`, `C_Velocity3D` |
| Color animation | `ANIMATION_COLOR` | *(see system header — requires C_ActionAnimation + C_AnimColorState + C_VoxelSetNew)* |
| Mouse hover glow | `HITBOX_MOUSE_TEST`, `SPAWN_GLOW` | `C_HitBox2D` |

Pipeline ordering for idle-bob: `PERIODIC_IDLE` → `PERIODIC_IDLE_POSITION_OFFSET`
→ modifier resolver pipeline → `PROPAGATE_TRANSFORM`. If `PROPAGATE_TRANSFORM`
is already registered in the demo (most are), only the first two need adding.
