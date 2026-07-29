#ifndef IR_PREFAB_SETTINGS_REGISTRY_H
#define IR_PREFAB_SETTINGS_REGISTRY_H

// World-scoped registry of live-togglable settings (#2551).
//
// A creation (or an engine prefab) registers a named setting backed by a
// getter/setter pair; `IRPrefab::SettingsMenu` renders one row per entry and
// applies edits back through the setter. Registration is the only wiring an
// adopter needs — the menu discovers everything from this registry:
//
//   IRPrefab::Settings::registerBool(
//       "CHECKERBOARD",
//       [] { return g_checkerboard; },
//       [](bool on) { g_checkerboard = on; reapplyVoxelTint(); }
//   );
//
// This is the *typed-settings* half of the discoverability pair. The other
// half — which key does what — is the command registry #2550 renders through
// `IRPrefab::HelpOverlay`. Two registries, two concerns, one surface each;
// the menu links to the overlay rather than duplicating its rows.
//
// Storage is a singleton component rather than a header global or a Meyers
// singleton: the setters capture creation-owned state, so the registry must
// die with the world that state belongs to. See `.claude/rules/cpp-globals.md`
// §"World-scoped settings / game state".

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace IRComponents {

// One registered setting. The public `registerBool` / `registerEnum` /
// `registerFloat` entry points take typed callbacks and adapt them onto the
// single float-valued pair stored here: a bool is 0/1, an enum is its index,
// a float is itself. One pair rather than three keeps the apply loop in
// `System<SETTINGS_MENU>` uniform — the widget kind is the only thing that
// varies — and keeps the entry from carrying five unused `std::function`s.
struct SettingEntry {
    enum class Kind : std::uint8_t { BOOL, ENUM, FLOAT };

    // Rendered as the row's label by the uppercase-only trixel font. Keep it
    // short (~20 chars) so the control still fits beside it.
    std::string name_;
    Kind kind_ = Kind::BOOL;

    // Read on every menu frame so a value changed elsewhere (a hotkey, a
    // script, another setting's setter) shows up in the open menu.
    //
    // **Neither may register a setting.** Registration push_backs into
    // `C_SettingsRegistry::settings_`, so a re-entrant one would reallocate
    // the vector — and destroy the `std::function` that is mid-call — while
    // the menu is iterating it. Settings are registered at init; a setter that
    // wants to reveal more knobs should toggle a flag the already-registered
    // rows read, not grow the registry.
    std::function<float()> get_;
    std::function<void(float)> set_;

    // ENUM only — one label per value, indexed by the value itself.
    std::vector<std::string> enumLabels_;

    // FLOAT only — slider bounds.
    float min_ = 0.0f;
    float max_ = 1.0f;
};

// Singleton row holding every registered setting. Registration order is
// render order, so an adopter groups related settings by registering them
// together.
struct C_SettingsRegistry {
    std::vector<SettingEntry> settings_;
};

} // namespace IRComponents

namespace IRPrefab::Settings {

// Singleton accessor. `ensureRegistrySingleton()` mirrors
// `widget_theme.hpp::ensureThemeSingleton()`: `singleton<T>()`'s first call is
// a `createEntity`, which must first run on the main thread during wiring
// rather than inside a first-frame `beginTick`.
inline IRComponents::C_SettingsRegistry &registry() {
    return IREntity::singleton<IRComponents::C_SettingsRegistry>();
}

inline void ensureRegistrySingleton() {
    registry();
}

/// Registers a checkbox-backed on/off setting.
inline void
registerBool(std::string name, std::function<bool()> get, std::function<void(bool)> set) {
    IRComponents::SettingEntry entry;
    entry.name_ = std::move(name);
    entry.kind_ = IRComponents::SettingEntry::Kind::BOOL;
    entry.get_ = [get = std::move(get)] { return get() ? 1.0f : 0.0f; };
    entry.set_ = [set = std::move(set)](float value) { set(value != 0.0f); };
    registry().settings_.push_back(std::move(entry));
}

/// Registers a dropdown-backed enumerated setting. @p labels is indexed by the
/// value @p get returns, so the labels must cover the full range the setter
/// accepts — an out-of-range value renders as the dropdown's "---" placeholder
/// rather than asserting.
inline void registerEnum(
    std::string name,
    std::vector<std::string> labels,
    std::function<int()> get,
    std::function<void(int)> set
) {
    IRComponents::SettingEntry entry;
    entry.name_ = std::move(name);
    entry.kind_ = IRComponents::SettingEntry::Kind::ENUM;
    entry.enumLabels_ = std::move(labels);
    entry.get_ = [get = std::move(get)] { return static_cast<float>(get()); };
    entry.set_ = [set = std::move(set)](float value) { set(static_cast<int>(value)); };
    registry().settings_.push_back(std::move(entry));
}

/// Registers a slider-backed continuous setting over [@p min, @p max].
inline void registerFloat(
    std::string name,
    float min,
    float max,
    std::function<float()> get,
    std::function<void(float)> set
) {
    IRComponents::SettingEntry entry;
    entry.name_ = std::move(name);
    entry.kind_ = IRComponents::SettingEntry::Kind::FLOAT;
    entry.min_ = min;
    entry.max_ = max;
    entry.get_ = std::move(get);
    entry.set_ = std::move(set);
    registry().settings_.push_back(std::move(entry));
}

} // namespace IRPrefab::Settings

#endif /* IR_PREFAB_SETTINGS_REGISTRY_H */
