#ifndef LUA_WIDGET_BINDINGS_H
#define LUA_WIDGET_BINDINGS_H

// IRGui widget Lua bindings — build the C++ widget framework
// (IRPrefab::Widget) entirely from Lua, with a Lua `onClick` that fires on
// click and a polling `wasClicked`.
//
//   local panel  = IRGui.makePanel(x, y, w, h, title?, drawBorder?, zOrder?)
//   local label  = IRGui.makeLabel(x, y, text, color?, size?)
//   IRGui.setLabelText(label, text)
//   local button = IRGui.makeButton(x, y, w, h, label, onClick?, size?)   -- onClick(id)
//   if IRGui.wasClicked(button) then ... end
//   local number = IRGui.makeNumberField(x, y, w, h, value, min, max, step?, size?)
//   local value  = IRGui.numberFieldValue(number)
//   IRGui.setNumberFieldValue(number, value)
//   IRGui.focus(textInput)                            -- keyboard focus, next INPUT frame
//   local region = IRGui.makeHitRegion(x, y, w, h, {
//       zOrder = 0, onHover = fn(id), onUnhover = fn(id), onClick = fn(id),
//   })                                                -- every key optional
//   IRGui.destroyWidget(region)
//   local image  = IRGui.makeImage(x, y, w, h, path)   -- path: image-base relative
//   IRGui.setImagePath(image, path)
//   local err    = IRGui.imageError(image)       -- nil when the PNG decoded
//   local gx, gy = IRGui.glyphStep(size?)
//   local w, h   = IRRender.getGuiCanvasSize()
//   IRGui.setCoversBelow(id, true)                 -- a deliberate overlay
//   for _, f in ipairs(IRGui.layoutReport(w?, h?)) do
//       -- f.kind == IRGui.LayoutKind.OVERLAP, f.a, f.b, f.x, f.y, f.w, f.h
//   end
//
// `layoutReport` is `IRPrefab::GuiAudit::report` (gui_layout_audit.hpp):
// against the live "gui" canvas, or an explicit canvas size for a headless
// test that has none.
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
// `size` is an `IRText.Size` value (absent or nil = LARGE) choosing the
// widget's glyph set; makeSlider / makeCheckbox / makeDropdown take it last
// too. A bad size raises before the widget exists.
//
// The optional arguments ahead of `size` that bind as Lua references
// (`color`, `onClick`) are plain `sol::object`s: a nil `sol::optional` of a
// reference type does not consume its argument slot, so the size after it
// would read the nil instead.
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
#include <irreden/script/lua_text_bindings.hpp>
#include <irreden/utility/path_utils.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gui_layout_audit.hpp>
#include <irreden/render/systems/system_widget_lua_dispatch.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/widgets.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <stdexcept>
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

inline float finiteNumber(const sol::object &value, const char *caller, const char *argument) {
    if (!value.valid() || value.get_type() != sol::type::number) {
        throw sol::error{std::string(caller) + ": " + argument + " must be a finite number"};
    }
    const float number = value.as<float>();
    if (!IRMath::isFinite(number)) {
        throw sol::error{std::string(caller) + ": " + argument + " must be a finite number"};
    }
    return number;
}

// An image path from Lua, resolved inside @p imageBase, which must already be
// canonical. IRGui is an engine-wide table, so the image widget must not become
// a host-file read for whatever script calls it: a path that
// `IRUtility::resolveContainedPath` refuses raises before the file is opened.
// The widget gets the resolved path, never the Lua string.
inline std::string containedImagePath(
    const std::filesystem::path &imageBase, const std::string &path, const char *caller
) {
    const std::optional<std::filesystem::path> resolved =
        IRUtility::resolveContainedPath(imageBase, path);
    if (!resolved) {
        throw sol::error{
            std::string(caller) + ": path '" + path +
            "' must be relative to the image base and stay inside it (no absolute path, "
            "no '..', no symlink out)"
        };
    }
    return resolved->string();
}

inline constexpr std::array<std::string_view, 4> kHitRegionOptionKeys{
    "zOrder", "onHover", "onUnhover", "onClick"
};

// @p imageBase confines every `makeImage` / `setImagePath` path; it is
// canonicalized once here, and a base that cannot be resolved throws.
inline void bindWidgets(LuaScript &script, const std::filesystem::path &imageBase) {
    std::error_code error;
    const std::filesystem::path canonicalImageBase =
        std::filesystem::weakly_canonical(imageBase, error);
    if (error) {
        throw std::invalid_argument("bindWidgets requires a valid image base");
    }

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
        [](int x, int y, std::string text, sol::object color, sol::object size) -> lua_Integer {
        const IRRender::TextSize textSize = textSizeFromLua(size, "IRGui.makeLabel");
        const IRMath::Color resolved =
            color.valid() ? colorFromLua(color) : IRMath::Color{0, 0, 0, 0};
        const IREntity::EntityId id =
            IRPrefab::Widget::makeLabel(IRMath::ivec2(x, y), std::move(text), resolved, textSize);
        return static_cast<lua_Integer>(id);
    };

    gui["makeButton"] = [prefabSystemIds](
                            int x,
                            int y,
                            int w,
                            int h,
                            std::string label,
                            sol::object onClick,
                            sol::object size
                        ) -> lua_Integer {
        const IRRender::TextSize textSize = textSizeFromLua(size, "IRGui.makeButton");
        if (onClick.valid() && onClick.get_type() != sol::type::function) {
            throw sol::error{"IRGui.makeButton: onClick must be a function"};
        }
        const IREntity::EntityId id = IRPrefab::Widget::makeButton(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            std::move(label),
            textSize
        );
        if (onClick.valid()) {
            resolveWidgetDispatch(prefabSystemIds, "IRGui.makeButton(onClick)")
                ->registerClickHandler(id, onClick.as<sol::protected_function>());
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

    // The PNG decodes and resamples during the call (main-thread setup work,
    // never a tick). `path` is relative to the image base; one that could
    // leave the base raises. A missing or undecodable file inside it still
    // builds the widget, drawn as a placeholder hatch; `imageError` then
    // returns the reason, naming the path. Drawing needs WIDGET_RENDER_IMAGE
    // in RENDER.
    gui["makeImage"] =
        [canonicalImageBase](int x, int y, int w, int h, const std::string &path) -> lua_Integer {
        if (w <= 0 || h <= 0) {
            throw sol::error{"IRGui.makeImage: w and h must be positive"};
        }
        return static_cast<lua_Integer>(IRPrefab::Widget::makeImage(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            containedImagePath(canonicalImageBase, path, "IRGui.makeImage")
        ));
    };
    gui["setImagePath"] = [canonicalImageBase](lua_Integer id, const std::string &path) {
        requireWidget<IRComponents::C_WidgetImage>(id, "IRGui.setImagePath", "makeImage widget");
        IRPrefab::Widget::setImagePath(
            static_cast<IREntity::EntityId>(id),
            containedImagePath(canonicalImageBase, path, "IRGui.setImagePath")
        );
    };
    gui["imageError"] = [](lua_Integer id) -> sol::optional<std::string> {
        const std::string &error =
            requireWidget<IRComponents::C_WidgetImage>(id, "IRGui.imageError", "makeImage widget")
                ->error_;
        if (error.empty()) {
            return sol::nullopt;
        }
        return error;
    };

    gui["wasClicked"] = [](lua_Integer id) -> bool {
        return IRPrefab::Widget::wasClicked(static_cast<IREntity::EntityId>(id));
    };

    gui["glyphStep"] = [](sol::object size) {
        const IRRender::GlyphMetrics metrics =
            IRRender::glyphMetrics(textSizeFromLua(size, "IRGui.glyphStep"));
        return std::make_tuple(metrics.stepX_, metrics.stepY_);
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
                           float value,
                           sol::object size) -> lua_Integer {
        if (!(minValue < maxValue)) {
            throw sol::error{"IRGui.makeSlider: min must be below max"};
        }
        const IRRender::TextSize textSize = textSizeFromLua(size, "IRGui.makeSlider");
        return static_cast<lua_Integer>(IRPrefab::Widget::makeSlider(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            std::move(label),
            minValue,
            maxValue,
            IRMath::clamp(value, minValue, maxValue),
            textSize
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

    gui["makeCheckbox"] = [](int x,
                             int y,
                             int w,
                             int h,
                             std::string label,
                             sol::optional<bool> checked,
                             sol::object size) -> lua_Integer {
        const IRRender::TextSize textSize = textSizeFromLua(size, "IRGui.makeCheckbox");
        return static_cast<lua_Integer>(IRPrefab::Widget::makeCheckbox(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            std::move(label),
            checked.value_or(false),
            textSize
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
                             sol::optional<int> itemHeight,
                             sol::object size) -> lua_Integer {
        // Parsed before the entity exists, so a malformed list creates nothing.
        std::vector<std::string> parsedItems = stringList(items, "IRGui.makeDropdown");
        const IRRender::TextSize textSize = textSizeFromLua(size, "IRGui.makeDropdown");
        const IREntity::EntityId widget = IRPrefab::Widget::makeDropdown(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            {},
            -1,
            itemHeight.value_or(h),
            textSize
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
    // Opens or closes the item strip as a click on the header would.
    gui["setDropdownOpen"] = [](lua_Integer id, bool open) {
        requireWidget<IRComponents::C_WidgetDropdown>(id, "IRGui.setDropdownOpen", "dropdown")
            ->isOpen_ = open;
    };

    gui["makeTextInput"] = [](int x,
                              int y,
                              int w,
                              int h,
                              sol::optional<std::string> text,
                              sol::optional<int> maxLength) -> lua_Integer {
        const int resolvedMaxLength = maxLength.value_or(0);
        if (resolvedMaxLength < 0) {
            throw sol::error{"IRGui.makeTextInput: maxLength must not be negative"};
        }
        return static_cast<lua_Integer>(IRPrefab::Widget::makeTextInput(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            text.value_or(std::string{}),
            resolvedMaxLength
        ));
    };
    gui["textInputValue"] = [](lua_Integer id) -> std::string {
        requireWidget<IRComponents::C_WidgetTextInput>(id, "IRGui.textInputValue", "text input");
        return IRPrefab::Widget::textInputValue(static_cast<IREntity::EntityId>(id));
    };
    gui["setTextInputValue"] = [](lua_Integer id, std::string text) {
        requireWidget<IRComponents::C_WidgetTextInput>(id, "IRGui.setTextInputValue", "text input");
        IRPrefab::Widget::setTextInputValue(static_cast<IREntity::EntityId>(id), std::move(text));
    };

    gui["focus"] = [](lua_Integer id) {
        requireWidget<IRComponents::C_Widget>(id, "IRGui.focus", "live widget");
        IRPrefab::Widget::focus(static_cast<IREntity::EntityId>(id));
    };

    gui["makeNumberField"] = [](int x,
                                int y,
                                int w,
                                int h,
                                sol::object value,
                                sol::object minValue,
                                sol::object maxValue,
                                sol::object step,
                                sol::object size) -> lua_Integer {
        constexpr const char *kCaller = "IRGui.makeNumberField";
        const float resolvedValue = finiteNumber(value, kCaller, "value");
        const float resolvedMin = finiteNumber(minValue, kCaller, "min");
        const float resolvedMax = finiteNumber(maxValue, kCaller, "max");
        if (!(resolvedMin < resolvedMax)) {
            throw sol::error{"IRGui.makeNumberField: min must be below max"};
        }
        float resolvedStep = 0.0f;
        if (step.valid() && step.get_type() != sol::type::lua_nil) {
            resolvedStep = finiteNumber(step, kCaller, "step");
            if (!(resolvedStep > 0.0f)) {
                throw sol::error{"IRGui.makeNumberField: step must be above zero"};
            }
        }
        const IRRender::TextSize textSize = textSizeFromLua(size, kCaller);
        return static_cast<lua_Integer>(IRPrefab::Widget::makeNumberField(
            IRMath::ivec2(x, y),
            IRMath::ivec2(w, h),
            resolvedValue,
            resolvedMin,
            resolvedMax,
            resolvedStep,
            textSize
        ));
    };
    gui["numberFieldValue"] = [](lua_Integer id) -> float {
        requireWidget<IRComponents::C_WidgetNumberField>(
            id,
            "IRGui.numberFieldValue",
            "number field"
        );
        return IRPrefab::Widget::numberFieldValue(static_cast<IREntity::EntityId>(id));
    };
    gui["setNumberFieldValue"] = [](lua_Integer id, sol::object value) {
        requireWidget<IRComponents::C_WidgetNumberField>(
            id,
            "IRGui.setNumberFieldValue",
            "number field"
        );
        IRPrefab::Widget::setNumberFieldValue(
            static_cast<IREntity::EntityId>(id),
            finiteNumber(value, "IRGui.setNumberFieldValue", "value")
        );
    };

    gui["setDisabled"] = [](lua_Integer id, bool disabled) {
        requireWidget<IRComponents::C_Widget>(id, "IRGui.setDisabled", "widget");
        IRPrefab::Widget::setDisabled(static_cast<IREntity::EntityId>(id), disabled);
    };
    gui["isHovered"] = [](lua_Integer id) -> bool {
        requireWidget<IRComponents::C_WidgetState>(id, "IRGui.isHovered", "widget");
        return IRPrefab::Widget::isHovered(static_cast<IREntity::EntityId>(id));
    };
    gui["isPressed"] = [](lua_Integer id) -> bool {
        requireWidget<IRComponents::C_WidgetState>(id, "IRGui.isPressed", "widget");
        return IRPrefab::Widget::isPressed(static_cast<IREntity::EntityId>(id));
    };

    // ── Layout audit ─────────────────────────────────────────────────────────
    sol::table layoutKind = lua.create_table();
#define IR_BIND_LAYOUT_KIND(name)                                                                  \
    layoutKind[#name] = static_cast<lua_Integer>(IRPrefab::GuiAudit::FindingKind::name)
    IR_BIND_LAYOUT_KIND(OVERLAP);
    IR_BIND_LAYOUT_KIND(TEXT_OVER_WIDGET);
    IR_BIND_LAYOUT_KIND(TEXT_CLIPPED);
    IR_BIND_LAYOUT_KIND(OUTSIDE_PARENT);
    IR_BIND_LAYOUT_KIND(OFFSCREEN);
#undef IR_BIND_LAYOUT_KIND
    gui["LayoutKind"] = layoutKind;

    gui["setCoversBelow"] = [](lua_Integer id, bool covers) {
        requireWidget<IRComponents::C_Widget>(id, "IRGui.setCoversBelow", "widget");
        IRPrefab::Widget::setCoversBelow(static_cast<IREntity::EntityId>(id), covers);
    };
    gui["layoutReport"] =
        [](sol::this_state state, sol::optional<int> w, sol::optional<int> h) -> sol::table {
        if (w.has_value() != h.has_value()) {
            throw sol::error{
                "IRGui.layoutReport: pass both the canvas width and height, or neither"
            };
        }
        const std::vector<IRPrefab::GuiAudit::Finding> findings =
            w.has_value() ? IRPrefab::GuiAudit::report(IRMath::ivec2(*w, *h))
                          : IRPrefab::GuiAudit::report();
        sol::state_view view{state};
        sol::table out = view.create_table(static_cast<int>(findings.size()), 0);
        for (std::size_t i = 0; i < findings.size(); ++i) {
            const IRPrefab::GuiAudit::Finding &finding = findings[i];
            out[i + 1] = view.create_table_with(
                "kind",
                static_cast<lua_Integer>(finding.kind_),
                "a",
                static_cast<lua_Integer>(finding.a_),
                "b",
                static_cast<lua_Integer>(finding.b_),
                "x",
                finding.pos_.x,
                "y",
                finding.pos_.y,
                "w",
                finding.size_.x,
                "h",
                finding.size_.y
            );
        }
        return out;
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

// The image base defaults to the run directory, the tree `IRFile` reads.
inline void bindWidgets(LuaScript &script) {
    std::error_code error;
    const std::filesystem::path runDirectory = std::filesystem::current_path(error);
    if (error) {
        throw std::runtime_error("bindWidgets could not resolve the run directory");
    }
    bindWidgets(script, runDirectory);
}

} // namespace IRScript::detail

#endif /* LUA_WIDGET_BINDINGS_H */
