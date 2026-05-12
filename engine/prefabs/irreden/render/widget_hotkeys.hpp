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

static_assert(
    static_cast<uint32_t>(IRInput::kNumKeyMouseButtons) <= 0xFFFF,
    "KeyMouseButtons exceeds 16-bit range; update HotkeyKey packing in widget_hotkeys.hpp"
);

inline HotkeyKey makeHotkeyKey(IRInput::KeyModifierMask mods, IRInput::KeyMouseButtons key) {
    return (static_cast<HotkeyKey>(mods) << 16) | static_cast<HotkeyKey>(key);
}

// Program-lifetime registry: safe to access from beginTick and from creation init.
//
// Lifetime contract: callbacks registered here capture creation-scoped state.
// If IRGame::World is ever destroyed and recreated (e.g. a future hot-reload),
// call clearHotkeyRegistry() before destroying the World to prevent stale
// std::function captures from being invoked against freed memory. In the
// current single-process editor flow, the registry outlives the process and
// teardown is not required.
inline std::unordered_map<HotkeyKey, HotkeyEntry> &getHotkeyRegistry() {
    static std::unordered_map<HotkeyKey, HotkeyEntry> registry;
    return registry;
}

// Remove all registered hotkeys. Call during World teardown if the World
// may be recreated within the same process (hot-reload, editor reset).
inline void clearHotkeyRegistry() {
    getHotkeyRegistry().clear();
}

// Remove a single hotkey registration by combo.
inline void unregisterHotkey(IRInput::KeyModifierMask modifiers, IRInput::KeyMouseButtons key) {
    getHotkeyRegistry().erase(makeHotkeyKey(modifiers, key));
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
