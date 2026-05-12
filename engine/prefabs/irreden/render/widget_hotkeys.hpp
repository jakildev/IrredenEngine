#ifndef IRREDEN_WIDGET_HOTKEYS_H
#define IRREDEN_WIDGET_HOTKEYS_H

#include <irreden/input/ir_input_types.hpp>
#include <irreden/ir_profile.hpp>

#include <functional>
#include <string>
#include <unordered_map>

namespace IRPrefab::Widget {

struct HotkeyEntry {
    std::string actionName_;
    std::function<void()> callback_;
};

// Packed key: high 16 bits = KeyModifierMask, low 16 bits = KeyMouseButtons enum value.
using HotkeyKey = uint32_t;

inline HotkeyKey makeHotkeyKey(IRInput::KeyModifierMask mods, IRInput::KeyMouseButtons key) {
    return (static_cast<HotkeyKey>(mods) << 16) | static_cast<HotkeyKey>(key);
}

// Program-lifetime registry: safe to access from beginTick and from creation init.
inline std::unordered_map<HotkeyKey, HotkeyEntry> &getHotkeyRegistry() {
    static std::unordered_map<HotkeyKey, HotkeyEntry> registry;
    return registry;
}

// Register a hotkey. Fires `callback` when `key` is pressed with `modifiers` held.
// Logs a warning and replaces any existing registration for the same combo.
inline void registerHotkey(
    const std::string &actionName,
    IRInput::KeyModifierMask modifiers,
    IRInput::KeyMouseButtons key,
    std::function<void()> callback
) {
    const HotkeyKey k = makeHotkeyKey(modifiers, key);
    auto &registry = getHotkeyRegistry();
    auto it = registry.find(k);
    if (it != registry.end()) {
        IRE_LOG_WARN(
            "Hotkey conflict: '{}' is already bound to this combo (replacing with '{}')",
            it->second.actionName_,
            actionName
        );
    }
    registry[k] = HotkeyEntry{actionName, std::move(callback)};
}

} // namespace IRPrefab::Widget

#endif /* IRREDEN_WIDGET_HOTKEYS_H */
