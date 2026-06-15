# Plan: Expose the widget framework to Lua (shared menu/HUD bindings)

- **Issue:** #1816
- **Model:** opus
- **Date:** 2026-06-14
- **Epic context:** cross-repo game GMTK '26 jam-readiness; consumer is the
  arcade jam-starter's TITLE / GAME_OVER Lua menus. Pairs with #1615 (shared
  render-glue promotion, **already merged** — `lua_render_bindings.hpp` is in
  master, so this task is **not** blocked on #1615).

## Scope

Promote the engine's existing C++-only widget framework
(`engine/prefabs/irreden/render/widgets.hpp`) into the **shared** Lua binding
layer so any creation can author menus/HUD in Lua — build a button + panel +
label, position/size them against GUI-canvas metrics, and route a button click
back into a **Lua** handler — with **no per-creation C++ widget binding**. This
is the engine half the game's Lua scene/state machine drives.

The deliverable is one coherent contract with three faces, because the consumer
(a Lua TITLE menu) needs all three at once:
1. Lua builders for **button / panel / label** (the Phase-0 minimum the issue
   names), returning entity ids.
2. A **Lua-reachable click→handler** path (today widgets are poll-only; this
   adds the callback bridge).
3. A way to **get the widget systems running** from a Lua creation (today only
   C++ hosts can instantiate engine prefab systems — see Verified state §5).

## Verified current state (confirmed against the code, not guesses)

**1. Widget framework — `engine/prefabs/irreden/render/widgets.hpp`, namespace
`IRPrefab::Widget`.** Free-function builders, each returns `IREntity::EntityId`
and `createEntity(...)`s a fixed component bundle:
- `makeButton(ivec2 pos, ivec2 size, string label)` (`:73`) → `C_Widget` +
  `C_GuiPosition` + `C_GuiElement` + `C_WidgetState` + `C_WidgetButton` +
  `C_HitBox2DGui`.
- `makePanel(ivec2 pos, ivec2 size, string title="", bool drawBorder=true,
  int zOrder=0)` (`:43`) and `makeLabel(ivec2 pos, string text,
  Color colorOverride={0,0,0,0})` (`:60`) — PANEL/LABEL carry **no**
  `C_HitBox2DGui` (don't consume mouse), so they don't participate in z-order
  routing.
- Also `makeSlider`/`makeCheckbox`/`makeList`/`makeDropdown` (out of Phase-0
  scope but trivially bindable with the same shape).
- Read helpers (`getComponent`-based, "called from creation logic, not a system
  tick"): `wasClicked(id)` (`:128`), `isHovered`, `isPressed`, `sliderValue`,
  `checkboxState`. Mutators: `setSliderValue`, `setCheckboxState`,
  `setDisabled`, `setButtonLabel`, `setLabelText` (`:150-194`).

**2. Click model is poll-based — no callback storage.** A click sets the
one-frame flag `C_WidgetState::fireAction_`
(`engine/prefabs/irreden/render/components/component_widget.hpp:59`), written by
`System<WIDGET_INPUT>` on mouse-release-while-hovered
(`engine/prefabs/irreden/render/systems/system_widget_input.hpp:185`).
Consumers poll `wasClicked(id)` next. There is **no on-click callback field** on
any widget component — the callback bridge is net-new (Approach §1).

**3. Widget systems — `System<SystemName::X>` specializations.**
`System<WIDGET_INPUT>` (`system_widget_input.hpp:57`); the SystemName enum has
`WIDGET_INPUT`, `WIDGET_APPLY_{SLIDER,CHECKBOX,LIST,DROPDOWN,RADIO,TEXT_INPUT,
SCROLL}`, `WIDGET_RENDER_{PANEL,LABEL,BUTTON,SLIDER,CHECKBOX,LIST,DROPDOWN,RADIO,
TEXT_INPUT,SCROLL,COLOR_SWATCH}` (`engine/system/include/irreden/system/
ir_system_types.hpp:190-214`). C++ hosts wire them via
`IRSystem::createSystem<IRSystem::WIDGET_INPUT>()` listed in a pipeline
(`creations/demos/ui_widgets/main.cpp:123`, also `ui_dockspace`, `voxel_editor`).

**4. Shared Lua binding layer.** `LuaScript::bindLuaDrivenEcs()`
(`engine/script/src/lua_script.cpp:373-642`) is the once-per-creation shared
binding entry; it calls `detail::bindRenderGlue(*this)` (`:640`). The #1615
render-glue pattern lives in
`engine/script/include/irreden/script/lua_render_bindings.hpp`
(`inline void bindRenderGlue(LuaScript&)`): create-if-absent then **extend
(never replace)** the `IRRender`/`IRGui` tables with lambdas that wrap existing
C++ entry points. This is the exact pattern to mirror.

**5. Lua pipeline composition needs C++ pre-registration.**
`IRSystem.systemId(SystemName.X)`
(`engine/script/include/irreden/script/lua_pipeline_bindings.hpp:223`) resolves
**only** for prefab systems the C++ host registered via
`LuaScript::registerPrefabSystem<N>()` (per-`LuaScript` `prefabSystemIds` map;
see `lua_script.hpp:78-91`, which caches a `createSystem<NAME>()` call). So a
Lua creation **cannot** instantiate the engine widget systems today — bindings
alone are insufficient; the widget systems must be registered as prefab systems
for Lua. `IRSystem.SystemName` is a **hand-listed** integer table in
`bindSystemNameEnum` (`lua_pipeline_bindings.hpp:78-179`) — a new SystemName not
added there resolves to nil in Lua.

**6. Lua-callback storage pattern (canonical).**
`engine/prefabs/irreden/common/modifier_lua.hpp:169-184`: capture a
`sol::function` by value in a C++ lambda, invoke via `fn.call<T>(...)`.
**Lifetime contract** (`:169-170`): the captured `sol::function` `lua_unref`s on
destruction, so `LuaScript` must outlive any `EntityManager` holding entities
that hold the function. Lua components already store `sol::function` fields
(type `FUNCTION`, `lua_component_data.hpp`).

**7. GUI canvas size — not exposed engine-side.** No `getGuiCanvasSize()` exists
in the engine (grep empty); `IRRender::getMainCanvasSizeTrixels()`
(`engine/render/include/irreden/ir_render.hpp:195`) is main-canvas only. The GUI
canvas is `RenderManager::m_guiCanvas` (`render_manager.hpp:141`); its size is
the `C_TriangleCanvasTextures.size_` of the `"gui"` canvas — see how
`mousePositionInGuiTrixels()` derives it (`layout.hpp:400-409`). The issue notes
`getGuiCanvasSize()` is exposed *game-side*; deliverable #2 wants it promoted to
the **engine** shared bindings.

**8. Enum rule.** `.claude/rules/cpp-lua-enums.md`: never string-name-match in
binding code; expose C++ enums as Lua int tables via the `IR_BIND_*` macro,
accept `lua_Integer`, range-check, cast.

## Approach — one opus PR, in order

**1. Lua-callback bridge (the net-new mechanism).**
- New component `C_WidgetLuaCallback` (`engine/prefabs/irreden/render/
  components/component_widget_lua_callback.hpp`) holding `sol::function onClick_`.
  Follow the existing Lua-component lifetime contract (Verified §6).
- New follower system `System<SystemName::WIDGET_LUA_CALLBACK>`
  (`engine/prefabs/irreden/render/systems/system_widget_lua_callback.hpp`),
  ordered **immediately after `WIDGET_INPUT`** in the GUI pipeline. It iterates
  `C_WidgetState` + `C_WidgetLuaCallback`, collects the entity ids whose
  `fireAction_` is set into a local vector **during** iteration, then invokes
  `cb.onClick_.call()` for each **after** iteration finishes (snapshot-then-call
  — a handler that spawns/destroys entities must not mutate the archetype graph
  mid-`forEachComponent`; see Gotchas).
- Add `WIDGET_LUA_CALLBACK` to the SystemName enum, the `System<>`
  specialization, and the hand-listed Lua SystemName table (Cross-system audit).

**2. Shared widget bindings.** New
`engine/script/include/irreden/script/lua_widget_bindings.hpp`,
`inline void bindWidgets(LuaScript&)`, called from `bindLuaDrivenEcs()` right
after `bindRenderGlue` (`lua_script.cpp:~640`), with the same idempotency guard.
Create-if-absent + extend an `IRWidget` table:
- Builders `IRWidget.button{pos={x,y}, size={x,y}, label=...}`,
  `IRWidget.panel{...}`, `IRWidget.label{pos=, text=, ...}` → return `EntityId`.
  Coerce `{x=,y=}` tables (or two ints) to `ivec2`; thin wrappers over
  `IRPrefab::Widget::make*`.
- `IRWidget.onClick(id, fn)` — validates `fn` is a function, attaches
  `C_WidgetLuaCallback{fn}` to the widget (the callback-registration entry).
- Poll/mutate passthroughs: `wasClicked`, `isHovered`, `setLabelText`,
  `setDisabled`, `setButtonLabel` (+ slider/checkbox if cheap).
- `IRWidget.installSystems(renderEvent)` convenience — calls
  `script.registerPrefabSystem<WIDGET_INPUT>()`, the `WIDGET_RENDER_*` set, the
  `WIDGET_APPLY_*` actually used, and `WIDGET_LUA_CALLBACK`, then appends them to
  `renderEvent`'s pipeline (or returns the `SystemId`s for the Lua creation to
  compose via `registerPipeline`). This resolves the Verified §5 gap — without
  it Lua-built widgets never tick/render. Confirm the exact
  `registerPrefabSystem<N>()` signature at implementation time.
- Any enumerated param (none in Phase-0 button/panel/label) → `IR_BIND_*` int
  table, never string match (enum rule).

**3. Promote GUI metrics.** Add `IRRender::getGuiCanvasSize()`
(`ir_render.hpp`/`.cpp`) returning the live `"gui"` canvas
`C_TriangleCanvasTextures.size_` (mirror `layout.hpp:400-409`; read live, not
cached — resize-safe). Expose in `bindRenderGlue` as
`IRRender.getGuiCanvasSize()` → `{x,y}` table.

**4. Acceptance vehicle — a Lua-authored menu.** Add a minimal Lua-driven demo
(new `creations/demos/lua_menu/` or extend an existing Lua-composed demo) whose
`main.lua` builds a panel + label + button, `IRWidget.onClick`s the button to a
Lua handler (flip a flag / change the label / `print`), and `installSystems`.
Headless `--auto-screenshot` shows the menu rendering; drive the click with the
synthetic-input path from #1794 (PR #1827 in flight) if available, else assert
the binding round-trips + widgets render and leave click-fire to the cross-host
smoke. State the chosen verification in the PR.

## Affected files

- `engine/prefabs/irreden/render/components/component_widget_lua_callback.hpp` — **new** `C_WidgetLuaCallback{sol::function onClick_}`.
- `engine/prefabs/irreden/render/systems/system_widget_lua_callback.hpp` — **new** `System<WIDGET_LUA_CALLBACK>` follower (snapshot-then-call).
- `engine/system/include/irreden/system/ir_system_types.hpp` — add `WIDGET_LUA_CALLBACK` to SystemName.
- `engine/script/include/irreden/script/lua_widget_bindings.hpp` — **new** `bindWidgets()` (`IRWidget` table + `installSystems`).
- `engine/script/include/irreden/script/lua_pipeline_bindings.hpp` — add `WIDGET_LUA_CALLBACK` to the hand-listed `bindSystemNameEnum` table.
- `engine/script/src/lua_script.cpp` — call `detail::bindWidgets(*this)` in `bindLuaDrivenEcs()`.
- `engine/render/include/irreden/ir_render.hpp` + `engine/render/src/ir_render.cpp` — `getGuiCanvasSize()`.
- `engine/script/include/irreden/script/lua_render_bindings.hpp` — bind `IRRender.getGuiCanvasSize()`.
- `creations/demos/lua_menu/` (new) — Lua-authored menu acceptance demo + CMake.
- whichever system-include/registration header maps `WIDGET_*` SystemNames to their `System<>` specializations (find via the audit grep).

## Acceptance criteria

- A creation builds a **button + panel + label entirely from Lua**, with a Lua
  click handler that fires when the button is clicked — no per-creation C++
  widget binding.
- The Lua-authored widgets render and respond to hover/click (the existing
  input stack drives them once `installSystems` wires the pipeline).
- `IRRender.getGuiCanvasSize()` returns the live GUI canvas size from Lua.
- The demo builds + runs headless on macOS (Metal) and Linux (GL); existing C++
  widget demos (`ui_widgets`, `ui_dockspace`, `voxel_editor`) still build & run.
- `bindWidgets` is idempotent and bound once in `bindLuaDrivenEcs` (shared path).

## Cross-system audit (SystemName enum addition + sol::function component)

- **`WIDGET_LUA_CALLBACK` SystemName** touches: the enum
  (`ir_system_types.hpp`), the `System<>` specialization (new system header),
  the hand-listed Lua table (`bindSystemNameEnum`), and any
  SystemName→specialization registration / name-string map. Grep the existing
  last widget enum `WIDGET_RENDER_COLOR_SWATCH` across the tree to enumerate
  every site the widget SystemNames are referenced and mirror it.
- **`C_WidgetLuaCallback` holds `sol::function`** → the LuaScript-outlives-
  EntityManager lifetime contract (Verified §6). Ensure component destruction
  during world teardown does not call into a destroyed Lua VM (no `onDestroy`
  that touches Lua; let sol2's ref-count handle unref).
- **`bindWidgets` added to `bindLuaDrivenEcs`** — idempotency guard like
  `bindRenderGlue`; extend `IRWidget`, never replace.
- **No public symbol removed** → Engine API removal rule N/A.

## Gotchas

- **ECS mid-iteration footgun.** A Lua `onClick` handler may create/destroy
  entities (e.g. "start game"). `System<WIDGET_LUA_CALLBACK>` must snapshot the
  fired-widget ids during `forEachComponent` and invoke the Lua callbacks
  **after** the iteration completes — never call into Lua (which may mutate the
  archetype graph) mid-iteration.
- **`fireAction_` is one-frame.** Order the dispatch system right after
  `WIDGET_INPUT` (which sets it) within the same tick, before it's cleared next
  frame.
- **sol::function lifetime** — see Cross-system audit.
- **Lua can't instantiate engine prefab systems without `registerPrefabSystem`.**
  `IRSystem.SystemName.WIDGET_INPUT` alone is not pipeline-able; `installSystems`
  exists precisely to register + wire them. Don't assume bare SystemName access
  is enough.
- **pos/size coercion** — accept `{x=,y=}` tables; validate and error clearly on
  malformed input.
- **`getGuiCanvasSize` reads the live canvas component**, not a cached value, so
  it stays correct across resize.
- **Bind once in the shared path** (`bindLuaDrivenEcs`), never per-creation —
  that's the whole point of the ticket.

## Decomposition — one opus task (not a stack)

Filed as `[sonnet]`, but the `fleet:opus` label is correct: the `sol::function`
component + lifetime contract, the snapshot-then-call ECS-reentrancy handling,
and the `registerPrefabSystem` system-wiring are design judgment a sonnet worker
would likely design-block on. The three faces are one contract the game's
TITLE/GAME_OVER menus consume whole, and they touch shared files
(`lua_script.cpp`, `ir_system_types.hpp`, `lua_pipeline_bindings.hpp`) — flat
siblings would risk the #1370 conflicting-parallel-PR mode. Keep as one PR. If
the demo/click-injection face genuinely balloons the diff past one reviewable
PR, use the normal step-8 follow-up escalation — do not pre-split.
