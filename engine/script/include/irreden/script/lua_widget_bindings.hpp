#ifndef LUA_WIDGET_BINDINGS_H
#define LUA_WIDGET_BINDINGS_H

// IRGui widget Lua bindings — build the C++ widget framework
// (IRPrefab::Widget) entirely from Lua, with a Lua `onClick` that fires on
// click and a polling `wasClicked`.
//
//   local panel  = IRGui.makePanel(x, y, w, h, title?, drawBorder?, zOrder?)
//   local label  = IRGui.makeLabel(x, y, text, color?)
//   IRGui.setLabelText(label, text)
//   local button = IRGui.makeButton(x, y, w, h, label, onClick?)   -- onClick(id)
//   if IRGui.wasClicked(button) then ... end
//   local region = IRGui.makeHitRegion(x, y, w, h, {
//       zOrder = 0, onHover = fn(id), onUnhover = fn(id), onClick = fn(id),
//   })                                                -- every key optional
//   IRGui.destroyWidget(region)
//   local gx, gy = IRGui.glyphStep()
//   local w, h   = IRRender.getGuiCanvasSize()
//
// `onClick` / `onHover` / `onUnhover` are stored in WIDGET_LUA_DISPATCH's
// state, NOT a component:
// the binding reaches that system through the LuaScript's prefab-system-id
// map (the same mechanism `IRSystem.systemId` uses), so a creation must
// `registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>()` and place it in the
// INPUT pipeline immediately after WIDGET_INPUT before registering handlers.
// The dispatcher passes the widget's EntityId as each callback's only
// argument.
//
// The C++ widget constructors take `IRMath::ivec2 pos, IRMath::ivec2 size`,
// not `(x, y, w, h)` — the binding composes the ivec2s, so Lua authors in
// plain ints (no per-creation math usertype needed).

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/systems/system_widget_lua_dispatch.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/widgets.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace IRScript::detail {

// Resolve the live WIDGET_LUA_DISPATCH instance via the LuaScript's
// prefab-system-id map, raising a Lua-visible error that points at the
// missing registration call (mirrors `resolveOverlapDispatch`).
// @p caller names the Lua call in the error, e.g. "IRGui.makeButton(onClick)".
inline IRSystem::System<IRSystem::WIDGET_LUA_DISPATCH> *resolveWidgetDispatch(
    const std::unordered_map<int, IRSystem::SystemId> *prefabSystemIds, const std::string &caller
) {
    auto it = prefabSystemIds->find(static_cast<int>(IRSystem::WIDGET_LUA_DISPATCH));
    if (it == prefabSystemIds->end()) {
        throw sol::error{
            caller + ": WIDGET_LUA_DISPATCH is not registered — the C++ creation must call "
                     "LuaScript::registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>() and add "
                     "it to the INPUT pipeline after WIDGET_INPUT before main.lua registers a "
                     "widget handler"
        };
    }
    auto *system =
        IRSystem::getSystemParams<IRSystem::System<IRSystem::WIDGET_LUA_DISPATCH>>(it->second);
    if (system == nullptr) {
        throw sol::error{caller + ": widget dispatch system params are null"};
    }
    return system;
}

// The `zOrder` cap every Lua-facing widget factory applies. Clamp instead of
// letting `C_Widget`'s ctor assert: the value is script-supplied data, not a
// programmer error, and IR_ASSERT compiles to nothing under IR_RELEASE, which
// would leave a release build silently inverting the expanded-dropdown hover
// tie-break the bias exists to win. Only the diagnostic is debug-visible.
inline int clampWidgetZOrder(int requestedZOrder, const char *caller) {
    const int clampedZOrder =
        IRMath::min(requestedZOrder, IRComponents::kWidgetDropdownOpenZBias - 1);
    if (clampedZOrder != requestedZOrder) {
        IRE_LOG_WARN(
            "{}: zOrder {} is at or above the dropdown-open bias {}; clamped to {} so "
            "expanded dropdown rows stay clickable",
            caller,
            requestedZOrder,
            IRComponents::kWidgetDropdownOpenZBias,
            clampedZOrder
        );
    }
    return clampedZOrder;
}

// One `IRGui.makeHitRegion` handler option: absent or nil reads as an empty
// (invalid) function; anything but a function raises.
inline sol::protected_function
optionalHandler(const sol::table &options, const char *key, const char *caller) {
    const sol::object value = options[key];
    if (!value.valid() || value.get_type() == sol::type::lua_nil) {
        return sol::protected_function{};
    }
    if (value.get_type() != sol::type::function) {
        throw sol::error{std::string(caller) + ": option '" + key + "' must be a function"};
    }
    return value.as<sol::protected_function>();
}

// The widget component `T` on entity `id`, or a Lua error naming the call and
// the kind of widget it needs. Per-frame Lua readers share the binding-time
// getComponent allowance: they do not run inside a tick.
template <typename T>
inline T *requireWidget(lua_Integer id, const char *caller, const char *kind) {
    const IREntity::EntityId widget = static_cast<IREntity::EntityId>(id);
    T *component = nullptr;
    if (id > 0 && IREntity::entityExists(widget)) {
        component = IREntity::getComponentOptional<T>(widget).value_or(nullptr);
    }
    if (component == nullptr) {
        throw sol::error{
            std::string(caller) + ": entity " + std::to_string(id) + " is not a " + kind +
            " — pass the id the matching IRGui.make* call returned"
        };
    }
    return component;
}

// A Lua array of strings (1..#t), or a Lua error naming the call.
inline std::vector<std::string> stringList(const sol::table &items, const char *caller) {
    std::vector<std::string> out;
    const std::size_t n = items.size();
    out.reserve(n);
    for (std::size_t i = 1; i <= n; ++i) {
        const sol::object item = items[i];
        if (item.get_type() != sol::type::string) {
            throw sol::error{std::string(caller) + ": items must be a list of strings"};
        }
        out.push_back(item.as<std::string>());
    }
    return out;
}

inline constexpr std::array<std::string_view, 4> kHitRegionOptionKeys{
    "zOrder", "onHover", "onUnhover", "onClick"
};

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
            clampWidgetZOrder(zOrder.value_or(0), "IRGui.makePanel")
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
            resolveWidgetDispatch(prefabSystemIds, "IRGui.makeButton(onClick)")
                ->registerClickHandler(id, std::move(onClick.value()));
        }
        return static_cast<lua_Integer>(id);
    };

    // Options are validated before the entity exists, so a bad call creates
    // nothing. Handlers need WIDGET_LUA_DISPATCH; a region with none still
    // takes hover and is pollable through `wasClicked`.
    gui["makeHitRegion"] =
        [prefabSystemIds](int x, int y, int w, int h, sol::optional<sol::table> options)
        -> lua_Integer {
        constexpr const char *kCaller = "IRGui.makeHitRegion";
        int zOrder = 0;
        sol::protected_function onHover;
        sol::protected_function onUnhover;
        sol::protected_function onClick;
        if (options.has_value()) {
            const sol::table &opts = options.value();
            for (const auto &[key, value] : opts) {
                const bool known =
                    key.get_type() == sol::type::string && std::find(
                                                               kHitRegionOptionKeys.begin(),
                                                               kHitRegionOptionKeys.end(),
                                                               key.as<std::string_view>()
                                                           ) != kHitRegionOptionKeys.end();
                if (!known) {
                    throw sol::error{
                        std::string(kCaller) +
                        ": unknown option; valid keys are zOrder, onHover, onUnhover, onClick"
                    };
                }
            }
            const sol::object zOrderValue = opts["zOrder"];
            if (zOrderValue.valid() && zOrderValue.get_type() != sol::type::lua_nil) {
                if (zOrderValue.get_type() != sol::type::number) {
                    throw sol::error{std::string(kCaller) + ": option 'zOrder' must be a number"};
                }
                zOrder = clampWidgetZOrder(zOrderValue.as<int>(), kCaller);
            }
            onHover = optionalHandler(opts, "onHover", kCaller);
            onUnhover = optionalHandler(opts, "onUnhover", kCaller);
            onClick = optionalHandler(opts, "onClick", kCaller);
        }
        const bool wantsHover = onHover.valid() || onUnhover.valid();
        IRSystem::System<IRSystem::WIDGET_LUA_DISPATCH> *dispatch =
            wantsHover || onClick.valid() ? resolveWidgetDispatch(prefabSystemIds, kCaller)
                                          : nullptr;

        const IREntity::EntityId id =
            IRPrefab::Widget::makeHitRegion(IRMath::ivec2(x, y), IRMath::ivec2(w, h), zOrder);
        if (wantsHover) {
            dispatch->registerHoverHandlers(id, std::move(onHover), std::move(onUnhover));
        }
        if (onClick.valid()) {
            dispatch->registerClickHandler(id, std::move(onClick));
        }
        return static_cast<lua_Integer>(id);
    };

    // Deferred like every entity destroy: the widget goes at the next
    // structural flush, so a widget handler may destroy its own widget. The
    // widget's Lua handlers go with it, and none of them runs after this
    // call, events already queued this frame included.
    gui["destroyWidget"] = [](lua_Integer id) {
        requireWidget<IRComponents::C_Widget>(id, "IRGui.destroyWidget", "live widget");
        IREntity::destroyEntity(static_cast<IREntity::EntityId>(id));
    };

    // Raises on a non-label id rather than attaching a C_WidgetLabel to it: a
    // label needs the C_Widget / C_GuiPosition siblings makeLabel creates.
    gui["setLabelText"] = [](lua_Integer id, std::string text) {
        requireWidget<IRComponents::C_WidgetLabel>(id, "IRGui.setLabelText", "label")->text_ =
            std::move(text);
    };

    gui["wasClicked"] = [](lua_Integer id) -> bool {
        return IRPrefab::Widget::wasClicked(static_cast<IREntity::EntityId>(id));
    };

    gui["glyphStep"] = []() {
        return std::make_tuple(IRRender::kGlyphStepX, IRRender::kGlyphStepY);
    };

    // ── Value widgets: slider, checkbox, dropdown ────────────────────────────
    // An editor polls these each frame (`sliderValue`, `checkboxState`,
    // `dropdownSelected`) and pushes values back with the setters. They need
    // WIDGET_APPLY_* in INPUT (after WIDGET_INPUT) to take the mouse and
    // WIDGET_RENDER_* in RENDER to draw. Dropdown indices are 1-based here
    // (0 = nothing selected) and 0-based in C++.

    gui["makeSlider"] = [](int x,
                           int y,
                           int w,
                           int h,
                           std::string label,
                           float minValue,
                           float maxValue,
                           float value) -> lua_Integer {
        if (!(minValue < maxValue)) {
            throw sol::error{"IRGui.makeSlider: min must be below max"};
        }
        return static_cast<lua_Integer>(IRPrefab::Widget::makeSlider(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            std::move(label),
            minValue,
            maxValue,
            IRMath::clamp(value, minValue, maxValue)
        ));
    };
    gui["sliderValue"] = [](lua_Integer id) -> float {
        requireWidget<IRComponents::C_WidgetSlider>(id, "IRGui.sliderValue", "slider");
        return IRPrefab::Widget::sliderValue(static_cast<IREntity::EntityId>(id));
    };
    gui["setSliderValue"] = [](lua_Integer id, float value) {
        requireWidget<IRComponents::C_WidgetSlider>(id, "IRGui.setSliderValue", "slider");
        IRPrefab::Widget::setSliderValue(static_cast<IREntity::EntityId>(id), value);
    };

    gui["makeCheckbox"] =
        [](int x, int y, int w, int h, std::string label, sol::optional<bool> checked)
        -> lua_Integer {
        return static_cast<lua_Integer>(IRPrefab::Widget::makeCheckbox(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            std::move(label),
            checked.value_or(false)
        ));
    };
    gui["checkboxState"] = [](lua_Integer id) -> bool {
        requireWidget<IRComponents::C_WidgetCheckbox>(id, "IRGui.checkboxState", "checkbox");
        return IRPrefab::Widget::checkboxState(static_cast<IREntity::EntityId>(id));
    };
    gui["setCheckboxState"] = [](lua_Integer id, bool checked) {
        requireWidget<IRComponents::C_WidgetCheckbox>(id, "IRGui.setCheckboxState", "checkbox");
        IRPrefab::Widget::setCheckboxState(static_cast<IREntity::EntityId>(id), checked);
    };

    gui["makeDropdown"] = [](int x,
                             int y,
                             int w,
                             int h,
                             const sol::table &items,
                             sol::optional<int> selected,
                             sol::optional<int> itemHeight) -> lua_Integer {
        // Parsed before the entity exists, so a malformed list creates nothing.
        std::vector<std::string> parsedItems = stringList(items, "IRGui.makeDropdown");
        const IREntity::EntityId widget = IRPrefab::Widget::makeDropdown(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            {},
            -1,
            itemHeight.value_or(h)
        );
        IRPrefab::Widget::setDropdownItems(
            widget,
            std::move(parsedItems),
            selected.value_or(1) - 1
        );
        return static_cast<lua_Integer>(widget);
    };
    gui["dropdownSelected"] = [](lua_Integer id) -> int {
        requireWidget<IRComponents::C_WidgetDropdown>(id, "IRGui.dropdownSelected", "dropdown");
        return IRPrefab::Widget::dropdownSelectedIndex(static_cast<IREntity::EntityId>(id)) + 1;
    };
    gui["setDropdownSelected"] = [](lua_Integer id, int index) {
        requireWidget<IRComponents::C_WidgetDropdown>(id, "IRGui.setDropdownSelected", "dropdown");
        IRPrefab::Widget::setDropdownSelectedIndex(static_cast<IREntity::EntityId>(id), index - 1);
    };
    gui["setDropdownItems"] =
        [](lua_Integer id, const sol::table &items, sol::optional<int> selected) {
            requireWidget<IRComponents::C_WidgetDropdown>(id, "IRGui.setDropdownItems", "dropdown");
            IRPrefab::Widget::setDropdownItems(
                static_cast<IREntity::EntityId>(id),
                stringList(items, "IRGui.setDropdownItems"),
                selected.value_or(1) - 1
            );
        };
    gui["isDropdownOpen"] = [](lua_Integer id) -> bool {
        requireWidget<IRComponents::C_WidgetDropdown>(id, "IRGui.isDropdownOpen", "dropdown");
        return IRPrefab::Widget::isDropdownOpen(static_cast<IREntity::EntityId>(id));
    };

    gui["setDisabled"] = [](lua_Integer id, bool disabled) {
        requireWidget<IRComponents::C_Widget>(id, "IRGui.setDisabled", "widget");
        IRPrefab::Widget::setDisabled(static_cast<IREntity::EntityId>(id), disabled);
    };
    gui["isHovered"] = [](lua_Integer id) -> bool {
        requireWidget<IRComponents::C_WidgetState>(id, "IRGui.isHovered", "widget");
        return IRPrefab::Widget::isHovered(static_cast<IREntity::EntityId>(id));
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
        // Per-frame Lua readers share the binding-time getComponent allowance;
        // they do not run inside a tick.
        const IRMath::ivec2 size =
            IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas).size_;
        return std::make_tuple(size.x, size.y);
    };
}

} // namespace IRScript::detail

#endif /* LUA_WIDGET_BINDINGS_H */
