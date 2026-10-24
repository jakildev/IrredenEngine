#ifndef IRREDEN_WIDGET_HOTKEYS_H
#define IRREDEN_WIDGET_HOTKEYS_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/input/ir_input_types.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_widget_hotkey.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace IRPrefab::Widget {

using HotkeyKey = std::uint32_t;
using HotkeyEntry = IRComponents::WidgetHotkeyCallback;

static_assert(
    static_cast<HotkeyKey>(IRInput::kNumKeyMouseButtons) <= 0xFFFFu,
    "KeyMouseButtons exceeds 16-bit range; update HotkeyKey packing"
);

inline HotkeyKey makeHotkeyKey(IRInput::KeyModifierMask mods, IRInput::KeyMouseButtons key) {
    return (static_cast<HotkeyKey>(mods) << 16) | static_cast<HotkeyKey>(key);
}

inline constexpr std::array<IRInput::KeyMouseButtons, 10> kFocusKeys{
    IRInput::kKeyButtonTab,
    IRInput::kKeyButtonSpace,
    IRInput::kKeyButtonEnter,
    IRInput::kKeyButtonEscape,
    IRInput::kKeyButtonUp,
    IRInput::kKeyButtonDown,
    IRInput::kKeyButtonHome,
    IRInput::kKeyButtonEnd,
    IRInput::kKeyButtonDelete,
    IRInput::kKeyButtonBackspace,
};

inline bool isFocusKey(IRInput::KeyMouseButtons key) {
    for (IRInput::KeyMouseButtons candidate : kFocusKeys) {
        if (candidate == key) {
            return true;
        }
    }
    return false;
}

inline bool isKeyboardHotkey(IRInput::KeyMouseButtons key) {
    return key > IRInput::kNullButton && key < IRInput::kMouseButtonNull;
}

inline bool
commandClaimsPress(IRInput::KeyMouseButtons key, IRInput::KeyModifierMask heldModifiers) {
    return IRCommand::isButtonBound(IRInput::KEY_MOUSE, IRInput::PRESSED, key, heldModifiers) ||
           IRCommand::isButtonBound(IRInput::KEY_MOUSE, IRInput::HELD, key, heldModifiers);
}

inline IRComponents::C_WidgetHotkeyRegistry &registry() {
    return IREntity::singleton<IRComponents::C_WidgetHotkeyRegistry>();
}

inline std::unordered_map<HotkeyKey, HotkeyEntry> &getHotkeyRegistry() {
    return registry().callbacks_;
}

inline void clearHotkeyRegistry() {
    getHotkeyRegistry().clear();
}

inline void unregisterHotkey(IRInput::KeyModifierMask modifiers, IRInput::KeyMouseButtons key) {
    getHotkeyRegistry().erase(makeHotkeyKey(modifiers, key));
}

struct HotkeyConflict {
    IREntity::EntityId widget_ = IREntity::kNullEntity;
    bool callback_ = false;
    bool command_ = false;
};

inline HotkeyConflict findHotkeyConflict(
    IRInput::KeyMouseButtons key,
    IRInput::KeyModifierMask modifiers,
    IREntity::EntityId exclude = IREntity::kNullEntity
) {
    HotkeyConflict result{};
    IREntity::forEachComponent<IRComponents::C_WidgetHotkey>(
        [&](IREntity::EntityId &id, IRComponents::C_WidgetHotkey &hotkey) {
            if (id != exclude && hotkey.key_ == key && hotkey.modifiers_ == modifiers &&
                (result.widget_ == IREntity::kNullEntity || id < result.widget_)) {
                result.widget_ = id;
            }
        }
    );
    result.callback_ = getHotkeyRegistry().contains(makeHotkeyKey(modifiers, key));
    result.command_ = commandClaimsPress(key, modifiers);
    return result;
}

inline bool validHotkey(IRInput::KeyMouseButtons key, IRInput::KeyModifierMask modifiers) {
    return isKeyboardHotkey(key) && !isFocusKey(key) && (modifiers & ~IRInput::kModifierAll) == 0;
}

inline void removeConflict(const HotkeyConflict &conflict, HotkeyKey packed) {
    if (conflict.widget_ != IREntity::kNullEntity) {
        IREntity::removeComponent<IRComponents::C_WidgetHotkey>(conflict.widget_);
    }
    if (conflict.callback_) {
        getHotkeyRegistry().erase(packed);
    }
}

inline bool setHotkey(
    IREntity::EntityId widget,
    IRInput::KeyMouseButtons key,
    IRInput::KeyModifierMask modifiers = IRInput::kModifierNone,
    std::string hint = ""
) {
    if (!IREntity::entityExists(widget) || !validHotkey(key, modifiers) ||
        !IREntity::getComponentOptional<IRComponents::C_Widget>(widget) ||
        !IREntity::getComponentOptional<IRComponents::C_WidgetState>(widget) ||
        !IREntity::getComponentOptional<IRComponents::C_HitBox2DGui>(widget)) {
        return false;
    }
    const HotkeyKey packed = makeHotkeyKey(modifiers, key);
    const HotkeyConflict conflict = findHotkeyConflict(key, modifiers, widget);
    if (conflict.widget_ != IREntity::kNullEntity || conflict.callback_ || conflict.command_) {
        IRE_LOG_WARN(
            "Widget hotkey collision for key {} modifiers {}",
            static_cast<int>(key),
            static_cast<int>(modifiers)
        );
    }
    removeConflict(conflict, packed);
    IREntity::setComponent(widget, IRComponents::C_WidgetHotkey{key, modifiers, std::move(hint)});
    return true;
}

inline void clearHotkey(IREntity::EntityId widget) {
    if (IREntity::entityExists(widget) &&
        IREntity::getComponentOptional<IRComponents::C_WidgetHotkey>(widget)) {
        IREntity::removeComponent<IRComponents::C_WidgetHotkey>(widget);
    }
}

inline void registerHotkey(
    const std::string &actionName,
    IRInput::KeyModifierMask modifiers,
    IRInput::KeyMouseButtons key,
    std::function<void()> callback
) {
    if (!validHotkey(key, modifiers)) {
        IRE_LOG_WARN("Refusing invalid widget callback hotkey");
        return;
    }
    const HotkeyKey packed = makeHotkeyKey(modifiers, key);
    const HotkeyConflict conflict = findHotkeyConflict(key, modifiers);
    if (conflict.widget_ != IREntity::kNullEntity || conflict.callback_ || conflict.command_) {
        IRE_LOG_WARN("Widget callback hotkey collision for '{}'", actionName);
    }
    removeConflict(conflict, packed);
    getHotkeyRegistry()[packed] = HotkeyEntry{actionName, std::move(callback)};
}

} // namespace IRPrefab::Widget

#endif /* IRREDEN_WIDGET_HOTKEYS_H */
