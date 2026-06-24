## Plan: Implement widget→Lua bindings (real code; #1816 shipped plan-only)

- **Issue:** #1975
- **Model:** opus (body suggested `[sonnet]`; raising for cross-module public surface + new pipeline system invoking LuaJIT — see *Model note*)
- **Date:** 2026-06-23

### Scope
Expose the C++ widget framework (`IRPrefab::Widget`) to Lua so a creation can
build a panel + label + button **entirely from Lua**, with a Lua `onClick`
that fires on click and a polling `wasClicked(id)`. Adds the one piece of
net-new infrastructure the framework lacks today — a **click→Lua dispatch
system** — plus the GUI-layout accessors (`IRRender.getGuiCanvasSize()`,
glyph step). #1816 closed on a docs-only PR (#1845); this is the real
implementation.

### Verified current state (source-checked, not guessed)
- **Widget C++ API** (`engine/prefabs/irreden/render/widgets.hpp`): the
  constructors take `IRMath::ivec2 pos, IRMath::ivec2 size`, **not** `(x,y,w,h)`
  as the issue body sketched. Exact signatures:
  - `makePanel(ivec2 pos, ivec2 size, string title="", bool drawBorder=true, int zOrder=0) -> EntityId` (`widgets.hpp:44`)
  - `makeLabel(ivec2 pos, string text, Color colorOverride={0,0,0,0}) -> EntityId` (`:60`)
  - `makeButton(ivec2 pos, ivec2 size, string label) -> EntityId` (`:74`) — **no `onClick` param today**
  - `wasClicked(EntityId) -> bool` (`:129`, reads `C_WidgetState::fireAction_`)
- **`C_WidgetState::fireAction_`** is a `bool`, set by `WIDGET_INPUT`
  (`engine/prefabs/irreden/render/systems/system_widget_input.hpp:58`).
- **IRGui Lua table** today has only `drawDisc`/`drawLine`, created
  extend-don't-replace in `bindRenderGlue`
  (`engine/script/include/irreden/script/lua_render_bindings.hpp:34-74`).
  `bindRenderGlue` is called from `bindLuaDrivenEcs()` at
  `engine/script/src/lua_script.cpp:652`, next to `bindCollisionEvents`.
- **The INPUT pipeline is composed per-creation**, not centrally — e.g.
  `creations/demos/ui_widgets/main.cpp:118-132` lists
  `WIDGET_INPUT -> WIDGET_APPLY_*`. So the new dispatch system is placed by the
  creation, immediately after `WIDGET_INPUT` (while `fireAction_` is fresh,
  before the per-kind render/clear systems).
- **There is an exact precedent for the click-dispatch piece**:
  `DISPATCH_LUA_OVERLAP`
  (`engine/prefabs/irreden/update/systems/system_dispatch_lua_overlap.hpp`
  + `engine/script/include/irreden/script/lua_collision_bindings.hpp`). It stores
  `sol::protected_function` handlers **in the System's SystemParams**, resolved
  from Lua via the prefab-system-id map (`script.prefabSystemIds()` +
  `getSystemParams<System<N>>(id)`), with error-trapped invoke. **This is the
  template to mirror** — and it shows the issue body's "function-local-static
  `unordered_map`" is wrong: it would violate `.claude/rules/cpp-systems.md`
  ("never function-local static for system state"). Use members on
  `System<WIDGET_LUA_DISPATCH>`.
- **sol2 in prefabs is already allowed** — `system_dispatch_lua_overlap.hpp`
  includes `<sol/sol.hpp>` and lives under `engine/prefabs/.../systems/`
  (header-only; sol2 materializes at the consuming creation's TU). So the new
  dispatch system lives alongside the other widget systems — **no CMake/layering
  change needed**. `engine/script` links System/Entity/sol2 and carries render
  include dirs, so the binding header compiles there.
- **Lua references a built-in system** via `IRSystem.systemId(SystemName.X)`,
  which needs (a) a `SystemName` enum entry, (b) an `IR_BIND_SYS(X)` line in
  `lua_pipeline_bindings.hpp`, (c) the creation calling
  `registerPrefabSystem<X>()`. (`engine/script/CLAUDE.md:698-783`.)
- **GUI metrics**: `IRRender::getCanvas("gui")` -> `EntityId`
  (`engine/render/include/irreden/ir_render.hpp:117`);
  `C_TriangleCanvasTextures::size_` is the `ivec2`
  (`.../components/component_triangle_canvas_textures.hpp:87`). Glyph step:
  `kGlyphStepX=8`, `kGlyphStepY=12`
  (`engine/render/include/irreden/render/trixel_font.hpp`).
- **Headless verification exists**: `IRVideo::createGuiTestSystem(GuiTestConfig)`
  fires scripted `GuiInputEvent` PRESS/RELEASE at button coords and runs an
  `onAssertFrame_` hook (`engine/video/include/irreden/video/auto_screenshot.hpp:140-200`,
  `IRPrefab::GuiTest::onFrame`). The `gui-verify` skill drives this — so "Lua
  onClick fires" is assertable without a human.

### Approach (one PR, in this order)
1. **New dispatch system** `engine/prefabs/irreden/render/systems/system_widget_lua_dispatch.hpp`
   — `template <> struct System<WIDGET_LUA_DISPATCH>`, mirroring
   `System<DISPATCH_LUA_OVERLAP>`:
   - Member registry `std::unordered_map<IREntity::EntityId, sol::protected_function> clickHandlers_;`
     (on the System, **not** function-local static).
   - `void registerClickHandler(EntityId widget, sol::protected_function fn)`.
   - Per-entity-id tick `void tick(IREntity::EntityId id, C_WidgetState &state)` —
     if `state.fireAction_`, look up `clickHandlers_` by `id` (a map lookup keyed
     by the iterating entity, **not** a `getComponent`, so it's allowed), invoke
     error-trapped (`result.valid()` -> `IRE_LOG_ERROR`). `C_WidgetState` is in
     the template params, so `fireAction_` arrives via column iteration.
   - `static SystemId create() { return registerSystem<WIDGET_LUA_DISPATCH, C_WidgetState>("WidgetLuaDispatch"); }`
     — **default (SERIAL) concurrency; do NOT mark PARALLEL_FOR** (sol2/LuaJIT
     single-threaded; same constraint `DISPATCH_LUA_OVERLAP` relies on).
   - Lifetime: handlers freed when `SystemManager` is destroyed, which `World`
     orders before `sol::state` — copy the lifetime comment verbatim.
2. **SystemName enum** — add `WIDGET_LUA_DISPATCH` in
   `engine/system/include/irreden/system/ir_system_types.hpp` immediately after
   `WIDGET_INPUT` (~line 191), in the widget group.
3. **Lua pipeline binding** — add `IR_BIND_SYS(WIDGET_LUA_DISPATCH)` in
   `engine/script/include/irreden/script/lua_pipeline_bindings.hpp`.
4. **New binding header** `engine/script/include/irreden/script/lua_widget_bindings.hpp`
   — `IRScript::detail::bindWidgets(LuaScript &script)`, mirroring
   `bindCollisionEvents` + `bindRenderGlue`:
   - A `resolveWidgetDispatch(prefabSystemIds)` helper returning
     `System<WIDGET_LUA_DISPATCH>*` via the prefab-system-id map, with a
     fail-fast `sol::error` naming the missing
     `registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>()` (copy
     `resolveOverlapDispatch`).
   - Extend (don't replace) `IRGui`:
     - `IRGui.makePanel(x,y,w,h, title?, drawBorder?, zOrder?)` ->
       `Widget::makePanel(ivec2(x,y), ivec2(w,h), ...)`, return `EntityId` as `lua_Integer`.
     - `IRGui.makeLabel(x,y,text, color?)` -> `Widget::makeLabel(...)`.
     - `IRGui.makeButton(x,y,w,h,label, onClick?)` -> `Widget::makeButton(...)`;
       if `onClick` is a function,
       `resolveWidgetDispatch(...)->registerClickHandler(id, std::move(fn))`.
       Return the `EntityId`.
     - `IRGui.wasClicked(id)` -> `Widget::wasClicked(EntityId(id))`.
     - `IRGui.glyphStep()` -> returns two ints `kGlyphStepX, kGlyphStepY`.
   - Extend (don't replace) `IRRender`:
     - `IRRender.getGuiCanvasSize()` -> read
       `getComponent<C_TriangleCanvasTextures>(getCanvas("gui")).size_`, return
       two ints `(w, h)`. (getComponent at binding call-time is fine — same
       allowance as the `Widget::*` readers, called from per-frame Lua, not a
       tick.) Guard a missing "gui" canvas with a clear `sol::error`.
   - Enum-as-table: no enumerated params on these constructors (no string-named
     params introduced), so nothing to bind under `cpp-lua-enums.md`.
5. **Wire it in** — in `bindLuaDrivenEcs()` (`engine/script/src/lua_script.cpp`),
   call `detail::bindWidgets(*this);` immediately after
   `detail::bindRenderGlue(*this);` (line 652). Add the `#include`.
6. **New demo creation** `creations/demos/lua_widgets/` proving the AC:
   - `main.cpp` — C++ host: standard render + INPUT pipeline (copy `ui_widgets`
     `initSystems`), inserting `WIDGET_LUA_DISPATCH` **right after**
     `WIDGET_INPUT`; `registerPrefabSystems<... WIDGET_LUA_DISPATCH ...>()`;
     `bindLuaDrivenEcs()` (which now calls `bindWidgets`) — **no per-creation
     widget binding**; run `main.lua`. Drive a scripted click + assertion via
     `createGuiTestSystem` when an auto-test flag is passed.
   - `main.lua` — builds panel + label + button **entirely from Lua** via
     `IRGui.make*`, registers a Lua `onClick` (logs/increments + sets a label),
     and a separate button polled with `IRGui.wasClicked`.
   - `CMakeLists.txt` (new) — copy `ui_widgets/CMakeLists.txt`, target
     `IRLuaWidgetsDemo`, `irreden_bundle_assets(... SCRIPTS main.lua)`.
   - **Register the target**: add
     `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/demos/lua_widgets)` to
     `creations/CMakeLists.txt` (silently won't build without it).

### Affected files
- `engine/prefabs/irreden/render/systems/system_widget_lua_dispatch.hpp` — **new** dispatch system (registry on System).
- `engine/system/include/irreden/system/ir_system_types.hpp` — add `WIDGET_LUA_DISPATCH` to `SystemName`.
- `engine/script/include/irreden/script/lua_pipeline_bindings.hpp` — `IR_BIND_SYS(WIDGET_LUA_DISPATCH)`.
- `engine/script/include/irreden/script/lua_widget_bindings.hpp` — **new** `bindWidgets`.
- `engine/script/src/lua_script.cpp` — `#include` + `bindWidgets` call after `bindRenderGlue` (~:652).
- `creations/demos/lua_widgets/{main.cpp,main.lua,CMakeLists.txt}` — **new** demo + headless gui-test.
- `creations/CMakeLists.txt` — `add_subdirectory(.../demos/lua_widgets)`.
- `.fleet/plans/issue-1975.md` — committed as the implementation branch's first commit.

### Acceptance criteria
- A creation builds a button + panel + label **entirely from Lua**, with a Lua
  `onClick` that **fires on click** — no per-creation C++ widget binding.
- `IRGui.wasClicked(id)` works for a pure-polling creation.
- `IRRender.getGuiCanvasSize()` is bound (and `IRGui.glyphStep()`).
- `IRLuaWidgetsDemo` builds; `gui-verify` (scripted click on the button)
  asserts the Lua `onClick` fired (PASS) headlessly.

### Model note
Body suggested `[sonnet]` ("expose existing C++ via the render-glue pattern").
Raising to **`[opus]`**: this adds a **new ECS system in the INPUT pipeline that
invokes LuaJIT** (concurrency + handler-lifetime invariants), touches the public
`ir_*` binding surface across **three engine modules** (script + system +
prefabs/render), and adds a new headless-tested creation. Each piece has an exact
precedent (`DISPATCH_LUA_OVERLAP`, `bindRenderGlue`, `GuiTest`), so it is
well-bounded — a plan reviewer may downgrade to sonnet if they judge the
pattern-following sufficient.

### Gotchas
- **Don't** use a function-local-static registry — `cpp-systems.md` forbids it;
  put `clickHandlers_` on `System<WIDGET_LUA_DISPATCH>` (copy `DISPATCH_LUA_OVERLAP`).
- **Don't** mark the dispatch system `PARALLEL_FOR` — sol2/LuaJIT are
  single-threaded; keep default SERIAL.
- The widget constructors take `ivec2`, not `(x,y,w,h)` — the Lua binding
  composes the `ivec2`s; there is no 4-int C++ overload.
- Placement matters: `WIDGET_LUA_DISPATCH` must run **after `WIDGET_INPUT`** (so
  `fireAction_` is set) and before the per-kind render/clear systems consume it,
  within the same INPUT pipeline pass.
- A destroyed widget leaves a stale `clickHandlers_` entry (bounded leak; the
  handler simply never fires again, same as `DISPATCH_LUA_OVERLAP`). Acceptable
  for v1 — do not add a per-frame scan to fix it; note a follow-up if cleanup is
  wanted.
- Keep the binding **extend-don't-replace** for both `IRGui` and `IRRender`
  (`if (!lua["..."].valid())`) so a creation's own entries survive.

— worker (opus, planning pass)

