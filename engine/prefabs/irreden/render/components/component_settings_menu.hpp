#ifndef COMPONENT_SETTINGS_MENU_H
#define COMPONENT_SETTINGS_MENU_H

#include <irreden/ir_entity.hpp>

namespace IRComponents {

// Singleton row holding whether the settings menu is open (#2551). Same
// world-scoped-state pattern as `C_HelpOverlayState`: a singleton component
// rather than a header global or a RenderManager field, per
// `.claude/rules/cpp-globals.md` and
// `engine/prefabs/irreden/render/CLAUDE.md` §"Exposing system public API".
//
// `Command<TOGGLE_SETTINGS_MENU>` flips it — a plain value mutation, safe from
// a command callback firing mid input-system tick. `System<SETTINGS_MENU>`
// reads it once per frame and spawns/destroys the menu's widget entities on
// the edges, so a creation that never opens the menu owns no widgets and pays
// one bool read per frame.
struct C_SettingsMenuState {
    bool open_ = false;
};

} // namespace IRComponents

namespace IRPrefab::SettingsMenu {

// Singleton accessor. `ensureStateSingleton()` mirrors
// `widget_theme.hpp::ensureThemeSingleton()`: the lazy `singleton<T>()` create
// is a `createEntity`, whose first call must run on the main thread, so the
// menu system touches it during pipeline wiring rather than in a first-frame
// `beginTick`. That touch also seeds the archetype the system iterates, so its
// tick fires from frame 1 instead of whenever something else creates the row.
inline IRComponents::C_SettingsMenuState &state() {
    return IREntity::singleton<IRComponents::C_SettingsMenuState>();
}

inline void ensureStateSingleton() {
    state();
}

inline void setOpen(bool open) {
    state().open_ = open;
}

inline void toggleOpen() {
    auto &s = state();
    s.open_ = !s.open_;
}

inline bool isOpen() {
    return state().open_;
}

} // namespace IRPrefab::SettingsMenu

#endif /* COMPONENT_SETTINGS_MENU_H */
