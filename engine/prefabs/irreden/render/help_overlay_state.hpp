#ifndef HELP_OVERLAY_STATE_H
#define HELP_OVERLAY_STATE_H

#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_help_overlay.hpp>

// Accessors for the help overlay's visibility singleton, split from the
// component the same way `widget_theme.hpp` splits from
// `components/component_widget_theme.hpp` — `components/` stays POD-only.
// This is a third header rather than the accessors living in the adoption
// surface (`help_overlay.hpp`) because that direction cycles:
// `help_overlay.hpp` includes `systems/system_help_overlay.hpp`, whose
// `create()` calls `ensureStateSingleton()`. Both the system and the
// adoption surface include this header instead.
namespace IRPrefab::HelpOverlay {

// Singleton accessor. The registration-time helper `ensureStateSingleton()`
// mirrors `widget_theme.hpp::ensureThemeSingleton()`: the lazy
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

#endif /* HELP_OVERLAY_STATE_H */
