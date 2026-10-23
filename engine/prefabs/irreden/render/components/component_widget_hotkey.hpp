#ifndef COMPONENT_WIDGET_HOTKEY_H
#define COMPONENT_WIDGET_HOTKEY_H

#include <irreden/ir_entity.hpp>
#include <irreden/input/ir_input_types.hpp>

#include <functional>
#include <string>
#include <unordered_map>

namespace IRComponents {

struct C_WidgetHotkey {
    IRInput::KeyMouseButtons key_ = IRInput::kNullButton;
    IRInput::KeyModifierMask modifiers_ = IRInput::kModifierNone;
    std::string hint_;
};

struct WidgetHotkeyCallback {
    std::string actionName_;
    std::function<void()> callback_;
};

struct C_WidgetHotkeyRegistry {
    std::unordered_map<std::uint32_t, WidgetHotkeyCallback> callbacks_;
};

} // namespace IRComponents

#endif /* COMPONENT_WIDGET_HOTKEY_H */
