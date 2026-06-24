#ifndef LUA_WIDGET_BINDINGS_H
#define LUA_WIDGET_BINDINGS_H

// IRGui widget Lua bindings (engine #1975) — build the C++ widget framework
// (IRPrefab::Widget) entirely from Lua, with a Lua `onClick` that fires on
// click and a polling `wasClicked`. Supersedes the docs-only close of #1816.
//
//   local panel  = IRGui.makePanel(x, y, w, h, title?, drawBorder?, zOrder?)
//   local label  = IRGui.makeLabel(x, y, text, color?)
//   local button = IRGui.makeButton(x, y, w, h, label, onClick?)   -- onClick(id)
//   if IRGui.wasClicked(button) then ... end
//   local gx, gy = IRGui.glyphStep()
//   local w, h   = IRRender.getGuiCanvasSize()
//
// `onClick` (the load-bearing new piece — click was poll-only before this)
// is stored in WIDGET_LUA_DISPATCH's session-lifetime state, NOT a component:
// the binding reaches that system through the LuaScript's prefab-system-id
// map (the same mechanism `IRSystem.systemId` uses), so a creation must
// `registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>()` and place it in the
// INPUT pipeline immediately after WIDGET_INPUT before registering handlers.
// The dispatcher passes the clicked widget's EntityId as the callback's only
// argument.
//
// The C++ widget constructors take `IRMath::ivec2 pos, IRMath::ivec2 size`,
// not `(x, y, w, h)` — the binding composes the ivec2s, so Lua authors in
// plain ints (no per-creation math usertype needed).

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/systems/system_widget_lua_dispatch.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/widgets.hpp>

#include <string>
#include <tuple>
#include <unordered_map>

namespace IRScript::detail {

// Resolve the live WIDGET_LUA_DISPATCH instance via the LuaScript's
// prefab-system-id map, raising a Lua-visible error that points at the
// missing registration call (mirrors `resolveOverlapDispatch`).
inline IRSystem::System<IRSystem::WIDGET_LUA_DISPATCH> *
resolveWidgetDispatch(const std::unordered_map<int, IRSystem::SystemId> *prefabSystemIds) {
    auto it = prefabSystemIds->find(static_cast<int>(IRSystem::WIDGET_LUA_DISPATCH));
    if (it == prefabSystemIds->end()) {
        throw sol::error{"IRGui.makeButton(onClick): WIDGET_LUA_DISPATCH is not registered "
                         "— the C++ creation must call "
                         "LuaScript::registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>() "
                         "and add it to the INPUT pipeline after WIDGET_INPUT before "
                         "main.lua registers an onClick"};
    }
    auto *system =
        IRSystem::getSystemParams<IRSystem::System<IRSystem::WIDGET_LUA_DISPATCH>>(it->second);
    if (system == nullptr) {
        throw sol::error{"IRGui.makeButton(onClick): widget dispatch system params are null"};
    }
    return system;
}

inline void bindWidgets(LuaScript &script) {
    sol::state &lua = script.lua();
    // Pointer to the live map (not a snapshot): the closure resolves the
    // dispatch system at call time, by which point the creation has
    // registered it. The map and the sol::state both outlive these closures
    // (owned by LuaScript), so capturing the raw pointer is safe.
    const std::unordered_map<int, IRSystem::SystemId> *prefabSystemIds = script.prefabSystemIds();

    // Extend (never replace) IRGui so a creation's own IRGui entries — and
    // the render-glue drawDisc/drawLine bound earlier — survive.
    if (!lua["IRGui"].valid()) {
        lua["IRGui"] = lua.create_table();
    }
    sol::table gui = lua["IRGui"];

    gui["makePanel"] = [](int x,
                          int y,
                          int w,
                          int h,
                          sol::optional<std::string> title,
                          sol::optional<bool> drawBorder,
                          sol::optional<int> zOrder) -> lua_Integer {
        const IREntity::EntityId id = IRPrefab::Widget::makePanel(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            title.value_or(std::string{}),
            drawBorder.value_or(true),
            zOrder.value_or(0)
        );
        return static_cast<lua_Integer>(id);
    };

    gui["makeLabel"] =
        [](int x, int y, std::string text, sol::optional<sol::object> color) -> lua_Integer {
        const IRMath::Color resolved =
            color.has_value() ? colorFromLua(color.value()) : IRMath::Color{0, 0, 0, 0};
        const IREntity::EntityId id =
            IRPrefab::Widget::makeLabel(IRMath::ivec2(x, y), std::move(text), resolved);
        return static_cast<lua_Integer>(id);
    };

    gui["makeButton"] = [prefabSystemIds](
                            int x,
                            int y,
                            int w,
                            int h,
                            std::string label,
                            sol::optional<sol::protected_function> onClick
                        ) -> lua_Integer {
        const IREntity::EntityId id = IRPrefab::Widget::makeButton(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            std::move(label)
        );
        if (onClick.has_value() && onClick.value().valid()) {
            resolveWidgetDispatch(prefabSystemIds)
                ->registerClickHandler(id, std::move(onClick.value()));
        }
        return static_cast<lua_Integer>(id);
    };

    gui["wasClicked"] = [](lua_Integer id) -> bool {
        return IRPrefab::Widget::wasClicked(static_cast<IREntity::EntityId>(id));
    };

    gui["glyphStep"] = []() {
        return std::make_tuple(IRRender::kGlyphStepX, IRRender::kGlyphStepY);
    };

    // Extend (never replace) IRRender so the render-glue setters bound earlier
    // (setSunDirection, setSkyColor, ...) survive.
    if (!lua["IRRender"].valid()) {
        lua["IRRender"] = lua.create_table();
    }

    lua["IRRender"]["getGuiCanvasSize"] = []() {
        const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        if (guiCanvas == IREntity::kNullEntity) {
            throw sol::error{"IRRender.getGuiCanvasSize: no \"gui\" canvas exists"};
        }
        // getComponent at binding call-time is fine — same allowance as the
        // IRPrefab::Widget::* readers, called from per-frame Lua, not a tick.
        const IRMath::ivec2 size =
            IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas).size_;
        return std::make_tuple(size.x, size.y);
    };
}

} // namespace IRScript::detail

#endif /* LUA_WIDGET_BINDINGS_H */
