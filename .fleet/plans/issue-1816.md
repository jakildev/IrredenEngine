# Plan: Expose the widget framework to Lua (shared menu/HUD bindings)

- **Issue:** #1816
- **Model:** sonnet (execution) — planned at opus
- **Date:** 2026-06-14

## Scope

Promote the engine's existing C++ widget framework (button / panel / label)
into the **shared** `bindLuaDrivenEcs` Lua surface so creations author menus
and HUD in Lua — including a Lua click handler that fires — with **no
per-creation C++ widget binding**. Pairs with the already-merged #1615
render-glue promotion.

## Verified current state

Confirmed by reading the actual code (not the issue body's guesses):

- **Widget framework is ECS-based, not plain structs.**
  `engine/prefabs/irreden/render/widgets.hpp` (namespace `IRPrefab::Widget`)
  exposes typed constructors:
  - `makePanel(ivec2 pos, ivec2 size, std::string title="", bool drawBorder=true, int zOrder=0)` — `widgets.hpp:43`
  - `makeLabel(ivec2 pos, std::string text, IRMath::Color colorOverride={0,0,0,0})` — `widgets.hpp:59`
  - `makeButton(ivec2 pos, ivec2 size, std::string label)` — `widgets.hpp:73`

  Each builds an entity from `C_Widget`, `C_GuiPosition`, `C_GuiElement`,
  `C_WidgetState`, a kind-specific data component (`C_WidgetButton` /
  `C_WidgetPanel` / `C_WidgetLabel`), and — for interactive kinds —
  `C_HitBox2DGui`. **None of these three constructors takes an enumerated
  param** (no alignment enum on label; color is a plain `Color`).

- **Click is poll-based, NOT callback-based.** `C_WidgetState::fireAction_`
  is set for exactly **one frame** by the `WIDGET_INPUT` system
  (`system_widget_input.hpp`) when a mouse-release lands over a hovered
  hitbox, then cleared at the next `WIDGET_INPUT` tick. Consumers poll
  `IRPrefab::Widget::wasClicked(id)` (`widgets.hpp:128`). There is **no**
  `std::function` / stored-callback machinery today — deliverable #3 must add
  the dispatch path.

- **The Lua-binding foundation already landed — #1615 is CLOSED/merged.**
  `engine/script/include/irreden/script/lua_render_bindings.hpp` defines
  `IRScript::detail::bindRenderGlue(LuaScript&)` which extends the `IRRender`
  and `IRGui` Lua tables using the `if (!lua["IRGui"].valid()) lua["IRGui"] =
  lua.create_table();` **extend-don't-replace** guard, and is called at
  `lua_script.cpp:640` inside `LuaScript::bindLuaDrivenEcs()` (`:373`). A
  `colorFromLua(sol::object)` helper already lives there for color params.

- **GUI metrics are only partially exposed.** Only `IRRender::getGuiScale()`
  (`ir_render.hpp:312`) is bound. There is **no** `getGuiCanvasSize` /
  glyph-metrics Lua binding. GUI canvas size is C++-reachable via
  `IRRender::getCanvas("gui")` (`ir_render.hpp:117`) + the canvas entity's
  `C_TriangleCanvasTextures::size_`; glyph step is the C++ constant
  `IRRender::kGlyphStepY`.

- **`sol::function` storage is already a supported pattern.**
  `lua_component_data.hpp` has a `FUNCTION` field type with
  `std::vector<sol::function>` columns; Lua callbacks held in C++ and invoked
  via `sol::protected_function` are established (see `lua_command_bindings.hpp`).

- **Mouse hover/click is already resolved in GUI-canvas trixel space** by
  `system_hitbox_mouse_test_gui.hpp` (computes the mouse-in-GUI position once
  per frame in `beginTick`) feeding `WIDGET_INPUT`. Nothing to add here.

- **The boilerplate being retired:** outside the widget framework's own
  render/input systems, the only consumers of the widget API are creations,
  which hand-write the same C++ pattern — hold each widget's `EntityId` in a
  C++ state struct and poll `IRPrefab::Widget::wasClicked(id)` every frame.
  This task moves that pattern behind the shared Lua surface so it isn't
  re-written per creation.

- **In-flight reconciliation:** #1827 (synthetic mouse/click injection,
  `fleet:approved`) and #1833 (GUI-test shot tables, stacked on #1827) supply
  headless click injection — a **synergy** for the acceptance test, not a
  dependency or a conflict. No open PR touches the widget bindings. #1615
  (the foundation) is merged. No contradiction with any active work.

## Approach — one task, `[sonnet]`

Single cohesive PR (the four deliverables are coupled: you can't demo a Lua
button without binding it, its click handler, and the metrics to place it).

1. **New binding module.** Add
   `engine/script/include/irreden/script/lua_widget_bindings.hpp` with
   `inline void IRScript::detail::bindWidgets(LuaScript &script)`, mirroring
   `bindRenderGlue`'s shape (extend-don't-replace the `IRGui` table). `#include`
   it in `lua_script.cpp` and call `detail::bindWidgets(*this);`
   **immediately after** the `detail::bindRenderGlue(*this);` line
   (`lua_script.cpp:640`).

2. **Constructor bindings into the `IRGui` table** (return the EntityId as a
   `lua_Integer` so Lua can hold a handle):
   - `IRGui.makePanel(x, y, w, h, title?, drawBorder?, zOrder?)` →
     `IRPrefab::Widget::makePanel(...)`.
   - `IRGui.makeLabel(x, y, text, color?)` → `makeLabel(...)` (reuse
     `colorFromLua` for the optional color).
   - `IRGui.makeButton(x, y, w, h, label, onClick?)` → `makeButton(...)`; if
     `onClick` (a `sol::function`) is passed, register it (step 4).
   - `IRGui.wasClicked(widget)` → forwards to `IRPrefab::Widget::wasClicked`
     (cheap one-liner; lets a creation choose pure polling instead of a
     callback).

3. **GUI metrics for positioning (deliverable #2).** Add a small
   `IRMath::ivec2 IRRender::getGuiCanvasSize()` accessor in
   `ir_render.hpp` (reads `getCanvas("gui")` → `C_TriangleCanvasTextures::size_`),
   bind it as `IRRender.getGuiCanvasSize()` (return two ints), and expose the
   glyph step (`IRRender.glyphStepY()` or a `getGlyphMetrics()` returning the
   x/y step from `IRRender::kGlyphStepY`). This is what lets Lua lay widgets out
   against canvas size without hard-coded pixels.

4. **Click callbacks routed to Lua (deliverable #3) — registry + deferred
   dispatch (NOT a system ticking over the widget archetype).** This design is
   the load-bearing decision; see Gotchas for why.
   - **Storage:** a function-local-static registry in `lua_widget_bindings.hpp`:
     ```cpp
     inline std::unordered_map<IREntity::EntityId, sol::protected_function>&
     widgetCallbacks() {
         static std::unordered_map<IREntity::EntityId, sol::protected_function> r;
         return r;
     }
     ```
     `IRGui.makeButton(..., onClick)` stores `widgetCallbacks()[id] = onClick;`.
     (Static, so the dispatch system needs no `LuaScript` handle. Keeps the
     `sol::protected_function` dependency in the **script** layer — the render
     widget prefab gains no sol2 coupling.)
   - **Dispatch:** a tiny **singleton** C++ system
     `system_widget_lua_dispatch.hpp` registered in the **INPUT** pipeline
     **immediately after `WIDGET_INPUT`** (so `fireAction_` is fresh this frame,
     not yet cleared). Its once-per-frame body:
     1. Read pass: for each `(id, fn)`, if the entity is gone, mark it for
        erase; else if `getComponent<C_WidgetState>(id).fireAction_`, collect
        `fn` into a local "to-fire" vector.
     2. Erase dropped entries from the registry.
     3. **After** the read pass, invoke each collected `fn()` as a
        `sol::protected_function` (log on error).
   - Add the `WIDGET_LUA_DISPATCH` SystemName enum entry and pipeline
     registration per the prefab `CLAUDE.md` (read it before editing the
     system manager / enum).

5. **Enum convention (deliverable #4).** Verified: `makeButton/makePanel/
   makeLabel` carry no enumerated params, so the rule
   (`.claude/rules/cpp-lua-enums.md`: integer tables, never string-name
   lookups) is satisfied by **not introducing any string-named kind/alignment
   param**. Do **not** add a `makeWidget("button", ...)` string dispatcher. If a
   later binding exposes `WidgetKind` generically, bind it as
   `IRComponent.WidgetKind.{PANEL,LABEL,BUTTON,...}` integer table (pattern:
   `lua_script.cpp:471-478`) — out of scope for this MVP.

6. **Acceptance demo / proof.** Add a minimal Lua snippet to an existing
   Lua-first creation (or a tiny new `creations/demos/` entry) that builds a
   panel + label + button with an `onClick` that sets a Lua-visible flag /
   logs, and run it to confirm the handler fires on click. Reference #1827's
   synthetic click injection for a future headless automated test, but a manual
   run satisfies acceptance now.

## Affected files

- **NEW** `engine/script/include/irreden/script/lua_widget_bindings.hpp` —
  `bindWidgets()` + `widgetCallbacks()` registry.
- `engine/script/src/lua_script.cpp` — include the header; call
  `detail::bindWidgets(*this);` after `:640`.
- **NEW** `engine/.../systems/system_widget_lua_dispatch.hpp` — singleton
  dispatch system (place alongside the other `WIDGET_*` systems; the worker
  confirms the exact dir from the prefab `CLAUDE.md`).
- SystemName enum + INPUT-pipeline registration (after `WIDGET_INPUT`).
- `engine/render/include/irreden/ir_render.hpp` (+ impl if needed) — add
  `getGuiCanvasSize()` accessor; bind GUI size + glyph metrics to Lua.
- Acceptance demo: minimal Lua creation snippet (panel + label + button +
  onClick).

## Acceptance criteria

- A creation builds a button + panel + label **entirely from Lua**, with a Lua
  click handler that fires on click.
- Widgets render and respond to mouse hover/click (input stack already exists).
- **No per-creation C++ widget binding** required.
- Build is clean for the demo target; manual run shows the click handler firing.

## Gotchas

- **ECS mid-iteration mutation footgun (the reason for the registry design).**
  A real menu click handler mutates the ECS — change scene, reset the world,
  open or close another panel — i.e. it creates/destroys entities. If callbacks were invoked from a system
  *ticking over the widget archetype*, that handler-side structural change could
  corrupt the iteration. The registry + **deferred** invocation (collect during
  the read pass, invoke after) means dispatch iterates a `std::unordered_map`,
  never an archetype — handler-side entity create/destroy is safe. Do **not**
  refactor this into a `(C_WidgetState, C_LuaWidgetAction)` system tick.
  Additionally confirm in the ECS `CLAUDE.md` whether entity create/destroy is
  command-buffered (deferred to a safe point); if it is applied immediately,
  keep the dispatch system at the tail of INPUT so its effects land before the
  next frame's systems.
- **`fireAction_` lives one frame.** Dispatch MUST run after `WIDGET_INPUT`
  sets it and before the next `WIDGET_INPUT` clears it — hence "INPUT pipeline,
  immediately after `WIDGET_INPUT`."
- **Stale EntityId.** A widget destroyed by a Lua handler (or scene change)
  leaves a dangling registry entry — guard every access with an entity-exists /
  `hasComponent<C_WidgetState>` check and erase dead entries each frame.
- **Layering.** Keep the `sol::protected_function` storage in the **script**
  layer (the binding header's static registry), NOT in a render-prefab
  component — the render/widget prefab must not gain a sol2/LuaJIT dependency.
- **Extend, don't replace, the Lua tables.** Use the
  `if (!lua["IRGui"].valid()) ... create_table()` guard (as `bindRenderGlue`
  does) so widget bindings coexist with the #1615 render-glue/`IRGui.drawDisc`
  entries.
- **Registry teardown.** Clear `widgetCallbacks()` on script teardown / world
  reset so stale `sol::protected_function`s don't outlive their `lua_State`
  (cross-refs the #1814 world-reset work; for this MVP a clear on
  `LuaScript` destruction suffices).
- **No string-named params.** Per `.claude/rules/cpp-lua-enums.md`, do not add
  any `"button"`/`"left"`-style string lookups; the typed constructors avoid
  enums entirely.
