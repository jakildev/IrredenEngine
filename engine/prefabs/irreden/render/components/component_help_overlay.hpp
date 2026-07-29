#ifndef COMPONENT_HELP_OVERLAY_H
#define COMPONENT_HELP_OVERLAY_H

namespace IRComponents {

// Singleton row holding the help overlay's visibility, read via
// `IRPrefab::HelpOverlay::` (`help_overlay_state.hpp`). World-scoped
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

#endif /* COMPONENT_HELP_OVERLAY_H */
