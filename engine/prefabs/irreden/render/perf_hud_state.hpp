#ifndef PERF_HUD_STATE_H
#define PERF_HUD_STATE_H

#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_perf_hud.hpp>

// Accessors for the perf HUD's state singleton, split from the component the
// same way `help_overlay_state.hpp` splits from
// `components/component_help_overlay.hpp` — `components/` stays POD-only, and
// the adoption surface (`perf_hud.hpp`) includes the system whose `create()`
// calls `ensureStateSingleton()`, so the accessors cannot live there without
// an include cycle.
namespace IRPrefab::PerfHud {

// Singleton accessor. `singleton<T>()`'s first call is a `createEntity`,
// which must run on the main thread, so every consumer touches it through
// `ensureStateSingleton()` from `create()` during pipeline wiring rather than
// inside a first-frame `beginTick`.
inline IRComponents::C_PerfHudState &state() {
    return IREntity::singleton<IRComponents::C_PerfHudState>();
}

inline void ensureStateSingleton() {
    state();
}

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

// Anchor corner. Setup-time: the system reads it once per frame, so a change
// lands on the next RENDER tick.
inline void setCorner(IRComponents::PerfHudCorner corner) {
    state().corner_ = corner;
}

inline IRComponents::PerfHudCorner corner() {
    return state().corner_;
}

} // namespace IRPrefab::PerfHud

#endif /* PERF_HUD_STATE_H */
