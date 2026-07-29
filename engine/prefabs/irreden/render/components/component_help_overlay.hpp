#ifndef COMPONENT_HELP_OVERLAY_H
#define COMPONENT_HELP_OVERLAY_H

#include <irreden/ir_entity.hpp>

namespace IRComponents {

// Singleton row holding the help overlay's visibility. World-scoped
// mutable state lives on a singleton component rather than a header global
// or a RenderManager field — see `.claude/rules/cpp-globals.md` and
// `engine/prefabs/irreden/render/CLAUDE.md` §"Exposing system public API".
// (`RenderManager::m_guiVisible`, which the legacy TOGGLE_GUI flag rides,
// is a pre-existing deviation, not a precedent to copy.)
//
// `Command<TOGGLE_HELP_OVERLAY>` flips it — a plain value mutation, safe
// from a command callback firing mid input-system tick. `System<HELP_OVERLAY>`
// reads it once per frame in `beginTick` and early-returns everywhere when
// hidden, so a creation that never opens the overlay pays nothing.
struct C_HelpOverlayState {
    bool visible_ = false;
};

} // namespace IRComponents

namespace IRPrefab::HelpOverlay {

// Singleton accessor. Registration-time helper `ensureStateSingleton()`
// below mirrors `widget_theme.hpp::ensureThemeSingleton()`: the lazy
// `singleton<T>()` create is a `createEntity`, whose first call must run on
// the main thread, so every consumer touches it from `create()` during
// pipeline wiring rather than inside a first-frame `beginTick`.
inline IRComponents::C_HelpOverlayState &state() {
    return IREntity::singleton<IRComponents::C_HelpOverlayState>();
}

inline void ensureStateSingleton() {
    state();
}

// Flip / query helpers so callers (the toggle command, a creation that wants
// the overlay open at startup, a GUI test predicate) never spell the
// singleton lookup themselves.
inline void setVisible(bool visible) {
    state().visible_ = visible;
}

inline void toggleVisible() {
    auto &s = state();
    s.visible_ = !s.visible_;
}

inline bool isVisible() {
    return state().visible_;
}

} // namespace IRPrefab::HelpOverlay

#endif /* COMPONENT_HELP_OVERLAY_H */
