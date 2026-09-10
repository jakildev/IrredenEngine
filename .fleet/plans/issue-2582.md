<!--
Plan file for #2582 (per #1932 — committed as the first commit of the
implementer's PR). This is the `## Plan` comment posted on issue #2582 on
2026-08-04, which cleared `fleet:plan-review` SOUND on 2026-08-07
(fleet-plan-lint exit 0; every verified-current-state anchor reproduced by the
reviewer against origin/master @ 12b8a49c8). Reproduced verbatim below, followed
by two sections the issue thread added after the plan-review cleared and the
2026-09-10 queue release bound into the plan:

  - "Post-review addendum" — the pre-destroy hook for `previousHoveredEntity`
    and its acceptance criterion (worker/pool-4 comment, 2026-08-07).
  - "Implementer's re-verification (2026-09-10)" — anchor drift since the plan
    was written, and the one shape refinement the addendum's criterion forces.

The issue thread is the canonical source.
-->

## Plan: input: migrate EntityEventHandlers off the process static to a singleton component (post-#2572)

- **Issue:** #2582
- **Model:** opus
- **Date:** 2026-08-04

### Scope

Migrate the world-scoped Lua handler registry from the Meyers-singleton
process static (`IRSystem::getEntityEventHandlers()`) to the sanctioned
singleton component `IRComponents::C_EntityEventHandlers`, retire the manual
engine-tail `clear()` contract #2572 added, and move the hover-detect
system's two remaining function-local statics (`previousHoveredEntity`,
the log-throttle counter) to member-on-`System<N>` state. One task, one PR.
The free-function accessor keeps its name, namespace, and method-call
surface — out-of-tree creations bind it into their own Lua tables.

### Verified current state

- The registry is a function-local static
  (`system_entity_hover_detect.hpp:131-134`); the engine tail calls
  `detail::clearEntityEventHandlers()` (`ir_engine.hpp:126-133` comment +
  call, defined `engine.cpp:33-35`) before `g_world.reset()` — the manual
  contract this ticket retires. #2572's fix is merged; this issue's
  dependency is satisfied.
- Consumer census (ran `fleet-rules-sweep --pattern
  'getEntityEventHandlers|addOnHovered|addOnClicked|onEntityHovered|onEntityClicked'`
  — 16 matches in 5 files over 3944 swept): the header itself, the
  `engine.cpp` tail call, `test/ecs/entity_event_handlers_test.cpp`, and
  prose (`docs/design/lua-physics-bindings.md`, the #2572 plan file).
  **The issue's "route IRScript's handler-registration bindings" step is
  stale** — there is no in-tree Lua registration binding
  (`lua-physics-bindings.md:131-136` documents exactly this gap); the
  registration surface lives in private creations layered on the engine,
  which call `IRSystem::getEntityEventHandlers().addOn*(...)` /
  `.removeHandler(...)` from their own binding code. So: no
  `engine/script/` edits, and the accessor's shape must not change
  (CLAUDE-BASELINE "Engine API removal rule" — local-grep-clean does not
  mean unconsumed; here consumers are positively known).
- No in-tree creation registers the hover system
  (`fleet-rules-sweep --pattern 'ENTITY_HOVER_DETECT' --glob
  '*.{cpp,hpp,lua}' creations` — 0 matches, 111 files swept, non-zero
  coverage), so the behavioral gate is the gtest, not a demo run.
- Teardown ordering is safe **by construction**: `World` declares `m_lua`
  before the manager block with a load-bearing comment
  (`world.hpp:42-51`, T-100 + #2446) precisely so archetype-column sol
  refs are released against a live `lua_State`; additionally
  `World::end()` runs `destroyAllEntities()` during `gameLoop()` while
  the VM is provably alive. A singleton component's
  `sol::protected_function`s are therefore dropped before the VM dies
  with no manual call — the exact invariant the static needed the tail
  hook for.
- Registration ordering: `m_entityManager` constructs in `World`'s ctor
  before any script path can run (`setupLuaBindings` / `runScript` are
  post-ctor), so the singleton facility exists before the earliest
  possible registration. The issue's premise holds.
- The one real hazard is lazy-create timing: `IREntity::singleton<T>()`
  lazy-creates via **eager** `createEntity`, which on the main thread
  inserts immediately with no frame-boundary assert
  (`entity_manager.hpp:172-198`). A *first* registration from a Lua
  callback fired inside a system tick would be an eager structural change
  mid-iteration — the silent address-invalidation footgun. The approach
  closes this with an unconditional seed at `World` construction.
  Pre-loop entity creation is the established pattern (every creation's
  entity builders run there; the modifier framework creates its singleton
  globals entity at bootstrap — `lua_script.hpp:108-112`).
- `.claude/rules/cpp-systems.md` "Live deviations" lists all three statics
  in this file (`:132` registry — pointed at this issue, `:140`
  previousHoveredEntity, `:182` logCounter). The issue's companion step
  "add a `cpp-globals.md` §Live deviations entry" is **superseded**: the
  migration lands on the spot, so the edit is *deleting* the three
  cpp-systems.md rows; cpp-globals.md stays at "None." (a function-local
  static was never a header namespace-scope global, so no
  `header_global_baseline` edit either). `.claude/rules/*.md` files are
  routinely edited by fleet PRs (e.g. #2868) — this is not gated
  self-config.
- Save-policy context: every new engine component needs an explicit
  `IR_SAVE_OPT_IN/OPT_OUT` + `AllEngineComponents` entry
  (`engine/world/CLAUDE.md` "New-component contract").
  `IR_SAVE_OPT_OUT(IRComponents::C_LambdaModifiers)`
  (`save_component_inventory.hpp:202`) is the callback-bearing precedent;
  `kExpectedEngineComponentCount` is 167 at `:552`.
  `SaveTrait.InventoryIsComplete` is **red on master** (#2834,
  pre-existing count drift) — this PR must stay delta-neutral on it.

### Approach

1. **New component header**
   `engine/prefabs/irreden/input/components/component_entity_event_handlers.hpp`:
   move the struct wholesale as `IRComponents::C_EntityEventHandlers` —
   four handler vectors, id counter, `addOn*`, `removeHandler`,
   `forEachHandlerVector`, `clear()`, `fire*` (all tier-b self-only
   methods). Rename public fields to trailing-underscore
   (`onHovered_`, …, `nextId_`; `HandlerEntry` members `id_`/`fn_`).
   Includes: sol2, `<vector>`, `<algorithm>`. Rewrite the #2572 lifecycle
   comment: refs now die at `destroyAllEntities()` / World teardown,
   while `m_lua` (declared before the manager block) is still alive.
2. **`system_entity_hover_detect.hpp`**: include the component header;
   delete the inline struct + static accessor. Re-point the accessor —
   `inline IRComponents::C_EntityEventHandlers &getEntityEventHandlers()
   { return IREntity::singleton<IRComponents::C_EntityEventHandlers>(); }`
   — and add `using EntityEventHandlers =
   IRComponents::C_EntityEventHandlers;` in `IRSystem` as out-of-tree
   spelling insurance. Migrate the system to member-on-`System<N>` via
   `registerSystem<ENTITY_HOVER_DETECT, C_EntityHoverDetectTag>("EntityHoverDetect")`:
   fields `previousHoveredEntity_ = IREntity::kNullEntity` and
   `logThrottleCounter_ = 0`; the no-op per-entity `tick`; the hover work
   moves verbatim into member `beginTick()` (it is the current
   `createSystem` third argument — `ir_system.hpp:135-140` confirms that
   slot is `functionBeginTick`). The read path uses
   `IREntity::singletonOrNull<C_EntityEventHandlers>()` — never
   lazy-creates mid-frame; guard the `fire*` calls on non-null, update
   `previousHoveredEntity_` unconditionally (null ≡ today's
   empty-vectors no-op).
3. **Seed at `World` ctor tail** (`world.cpp`, after
   `resizeWorkerStaging`): include the component header;
   `IREntity::singleton<IRComponents::C_EntityEventHandlers>();`
   (optionally `setName(..., "entityEventHandlers")` for diagnostics).
   Comment the why: every later accessor call is then non-structural, so
   a Lua-callback registration mid-tick can never eager-create
   mid-iteration.
4. **Delete the manual-clear contract**: `engine.cpp` —
   `detail::clearEntityEventHandlers()` + the hover-detect include;
   `ir_engine.hpp` — the declaration, the `gameLoop()` call, and both
   comment blocks (rewrite the `gameLoop` comment: handler-ref release is
   now owned by World teardown ordering, nothing left to call).
5. **Save inventory** (`save_component_inventory.hpp`):
   `IR_SAVE_OPT_OUT(IRComponents::C_EntityEventHandlers)` with the
   callback-bearing rationale (same class as `C_LambdaModifiers` — sol
   refs are session-local and cannot honestly round-trip; never write a
   serializer that substitutes defaults), plus the
   `AllEngineComponents` tuple entry, plus `kExpectedEngineComponentCount`
   167 → 168 in the same change (delta-neutral vs #2834's pre-existing
   red — do not fix that drift here).
6. **Re-point the regression test**
   (`test/ecs/entity_event_handlers_test.cpp`): fixture gains
   `IREntity::EntityManager` declared **after** `IRScript::LuaScript`
   (member order is load-bearing — `test/CLAUDE.md` Lua-seam pattern);
   `TearDown`'s static-clear becomes `destroyAllEntities()`. Extend with:
   (a) first accessor call materializes the singleton row
   (`singletonEntityOrNull<C_EntityEventHandlers>() != kNullEntity`),
   (b) a registered handler **observably fires** — `fireClicked` bumps a
   Lua-side counter, assert it reads 1,
   (c) after `destroyAllEntities()`, `singletonOrNull` returns null and
   the fixture tears down clean — the refs-die-before-VM lock that
   replaces the #2572 static-destructor lock.
   Keep the existing clear-empties-all-four case (re-pointed).
7. **Docs/registers, same PR**: `cpp-systems.md` — delete the three rows
   for this file and re-stamp the register's measured count/date;
   `engine/prefabs/irreden/input/CLAUDE.md` — document the component
   under Key components (world-scoped, survives `resetGameplay` like all
   singletons — identical to the old static's scene-transition behavior —
   dies at `destroyAllEntities`); `engine/world/CLAUDE.md` — add
   `C_EntityEventHandlers` to the callback-bearing opt-out list in the P7
   section; `docs/design/lua-physics-bindings.md:131-136` — re-word
   "process static" to singleton component (its "no in-tree Lua entry
   point" claim stays true); `test/world/save_trait_test.cpp` — add
   `EXPECT_FALSE(shouldSave<C_EntityEventHandlers>())` beside the
   callback-bearing opt-outs.

Suggested commit order: 1+2 (one compile unit), 5 (build gate), 3, 4, 6, 7.

### Affected files

- `engine/prefabs/irreden/input/components/component_entity_event_handlers.hpp` — new
- `engine/prefabs/irreden/input/systems/system_entity_hover_detect.hpp` — struct out, accessor re-pointed, member-on-System migration
- `engine/world/src/world.cpp` — ctor-tail singleton seed
- `engine/engine.cpp` — delete `detail::clearEntityEventHandlers()`
- `engine/include/irreden/ir_engine.hpp` — delete declaration + tail call + comments
- `engine/world/include/irreden/world/save_component_inventory.hpp` — opt-out + tuple + count
- `test/ecs/entity_event_handlers_test.cpp` — fixture + new assertions
- `test/world/save_trait_test.cpp` — opt-out assertion
- `.claude/rules/cpp-systems.md` — deviation rows removed, count re-stamped
- `engine/prefabs/irreden/input/CLAUDE.md`, `engine/world/CLAUDE.md`, `docs/design/lua-physics-bindings.md` — prose sync

### Acceptance criteria

- `fleet-build --target IrredenEngineTest` green;
  `fleet-run IrredenEngineTest --gtest_filter='EntityEventHandlers*'`
  passes with the positive-fire assertions: registration lands (vector
  size 1), the fired handler's Lua counter reads 1, and post-
  `destroyAllEntities` the singleton is gone. The fixture file exists
  (this is an extension of `test/ecs/entity_event_handlers_test.cpp`).
- Full-suite delta vs master is exactly the added/renamed cases;
  `SaveTrait.InventoryIsComplete` keeps its pre-existing #2834 delta
  (run the suite on master first if in doubt; never "fix" the count
  beyond this PR's +1/+1).
- `fleet-rules-sweep --pattern 'static EntityEventHandlers|clearEntityEventHandlers'`
  over the tree: zero code hits (prose/plan-file mentions exempt).
- `cmake --build <dir> --target header-checks` green (no new
  namespace-scope header globals introduced by the move).
- `fleet-build --target IRShapeDebug` + a standard `--auto-screenshot`
  smoke run: boots and exits clean (hover system unregistered there —
  proves the seeded-but-unconsumed path is inert).

### Gotchas

- **Merge-order conflict is expected on the save inventory**: in-flight
  PR #2897 (issue #2563) also adds a component + tuple entry + count
  bump. Whoever lands second rebases: keep both entries, sum the bumps,
  and re-read the count literal at rebase time instead of trusting this
  plan's "167".
- The trailing-underscore field rename must not touch the **method**
  names (`addOnHovered` etc.) — out-of-tree consumers call methods only.
- The component holds sol objects in an archetype column — sanctioned by
  T-100 (`world.hpp` comment); do not "fix" that during review.
- `C_EntityEventHandlers` is non-trivially-copyable and opted OUT — do
  not write a `SaveSerialize` for it.
- Tests must not cache the component pointer/reference across
  `destroyAllEntities()` — the cache resets and the next access
  lazy-recreates a fresh row.
- Use `singletonOrNull` in `beginTick` (defense in depth), the
  lazy-create `singleton<>` only in the accessor + World seed.
- System recreation within one World would reset `previousHoveredEntity_`
  to null (one spurious missed-unhover at most). No prefab-system
  recreation path exists today; this is the rule-prescribed shape and is
  what makes a future second World start clean — the other half of the
  multi-world hazard the #2573 review named.
- The PR body should state explicitly that the issue's cpp-globals.md
  companion step was superseded by migrating on the spot, so a reviewer
  doesn't flag the Area-listed file as unaddressed.
- Plans and PR bodies are engine-public: keep consumer references generic
  ("private creations layered on the engine"), no overlay specifics.

### Binding constraints from the plan review (2026-08-07, opus-reviewer/pool-9)

1. **Acceptance criterion 5 can go vacuously green.** `fleet-run`'s default
   watchdog reports a *truncated* run as `ALIVE-TIMEOUT` with **exit 0**, so
   "`IRShapeDebug` boots and exits clean" is satisfiable by a run that was cut
   off. Pass `--timeout 0` **before** the target and read the verdict line
   directly — an exit code alone is not evidence here.
2. **The `fleet-rules-sweep 'static EntityEventHandlers|clearEntityEventHandlers'`
   criterion is a negative gate** — record its coverage line. Exit 1 (zero
   matches, non-zero coverage) is the pass; exit 2 (scope resolved to 0 files) is
   the guard firing, not a clean tree.
3. Rebase onto **#2895** (non-inserting `getComponentOptional`, the path
   `singletonOrNull` reads through) rather than being surprised by it; **#2897**
   (save inventory) and **#2475** (`world.cpp`) touch the same seams.
4. `SaveTrait.InventoryIsComplete` red-on-master is **#2834** and is never
   yours — delta-neutral only.

---

## Post-review addendum (2026-08-07, worker/pool-4) — binding

Raised on the issue after the plan-review cleared, and bound into the plan by
the 2026-09-10 queue release. **Relocating `previousHoveredEntity` to a
`System<N>` member does not discharge the `resetGameplay` dangle lane.**

Approach step 2 gives the state an *owner*, which is the `cpp-globals.md` goal,
but a `System<N>` member survives `resetGameplay()` identically to a
function-local static — `engine/entity/CLAUDE.md` §"Scene-transition reset":
*"System-internal / Lua-global state persists — systems are never destroyed."*
The single-world lane is live today and strictly more reachable than the
multi-world one the issue body names (multi-world does not exist;
`resetGameplay` does, and `creations/demos/scene_reset` +
`creations/demos/persist_roundtrip` both call it):

1. Mouse hovers a gameplay entity `E` (carries `C_HitBox2D` — not
   `C_Persistent`, not a singleton) ⇒ `previousHoveredEntity_ = E`.
2. `IREntity::resetGameplay()` destroys `E`.
3. Next `beginTick`: both hitbox scans find nothing, so
   `currentHovered != previousHoveredEntity_` and
   `previousHoveredEntity_ != kNullEntity` ⇒ **`fireUnhovered(previousHoveredEntity_)`
   fires with the destroyed id**, handed to every registered Lua
   `onEntityUnhovered` handler as `entry.fn(entityId)`.

**This one cannot reuse PR #2866's shape as-written.** The two sites #2866 fixed
(`textEntity_`, `pivotIndicator_`) each had a `== kNullEntity` lazy-respawn
guard, so nulling the handle in a pre-destroy hook was sufficient — the existing
branch rebuilt. `previousHoveredEntity_` has no such guard: the id is used
directly as an **event payload**, so nulling it in a hook is the only remedy
that works, and it has a deliberate behavioural consequence — the unhover event
for the destroyed entity is **suppressed** rather than delivered against a
corpse.

### Additional approach step (no re-scope of the singleton migration)

2b. Register a pre-destroy hook in `System<ENTITY_HOVER_DETECT>::create()`
    alongside the migration — the `EntityManager::registerPreDestroyHook`
    pattern from `system_perf_stats_overlay.hpp:155-162` — nulling
    `previousHoveredEntity_` when the destroyed id matches.

### Additional acceptance criterion

- After `resetGameplay()` with an entity hovered, the next hover transition
  fires **no** `onEntityUnhovered` for the destroyed id. Positive control:
  revert the hook, confirm the test fails. `test/render/perf_stats_overlay_reset_test.cpp`
  is the reference shape.

---

## Implementer's re-verification (2026-09-10) — drift and one shape refinement

Re-checked every anchor against `origin/master` @ `7567858e5` before writing
code. Recorded here so a reviewer can tell which numbers are the plan's and
which are the tree's.

**Anchors that held exactly:** registry static `:131-134`;
`previousHoveredEntity` `:140`; `logCounter` `:182`;
`clearEntityEventHandlers` defined `engine.cpp:33-35` and called at the
`gameLoop()` tail; `IR_SAVE_OPT_OUT(C_LambdaModifiers)` at `:202`;
`cpp-systems.md` rows `:132` / `:140` / `:182`; the `resizeWorkerStaging`
ctor-tail seam.

**Drift:**
- `kExpectedEngineComponentCount` is **167 at `:555`**, not `:552` — value
  unchanged, line moved. 167 → 168 stands.
- The plan's three named in-flight PRs (**#2897**, **#2895**, **#2475**) have
  all **merged**, so the anticipated save-inventory merge conflict did not
  materialise; 167 is the literal read at implementation time, not the plan's
  stale guess.
- `docs/design/lua-physics-bindings.md`'s "process static" prose now sits at
  `:129-138` (content unchanged).
- #2834 is still **open** — `SaveTrait.InventoryIsComplete` stays red on
  master and this PR stays delta-neutral on it.

**Shape refinement forced by the addendum's acceptance criterion.** The
criterion is behavioural ("fires no `onEntityUnhovered`"), but `beginTick()` as
the plan describes it resolves `currentHovered` through
`IRRender::getEntityIdAtMouseTrixel()` and `IRInput::checkKeyMouseButton()` —
both out-of-line free functions that go through manager globals a headless gtest
has no GL context to construct. Asserting only "the member is null" would test
the mechanism, not the behaviour the criterion names.

So `beginTick()` is split at exactly one seam: it resolves `currentHovered`
from the three sources (unchanged, still `beginTick`), then delegates the
state-transition + click dispatch to member
`applyHoverTransition(EntityId currentHovered)`. The transition body moves
verbatim; nothing about the per-frame path changes. The gtest drives
`applyHoverTransition` directly, which makes the addendum's criterion a real
firing assertion with a real positive control (revert the hook ⇒ the
suppression test fails). This is the smallest change that keeps the criterion
executable rather than aspirational.
