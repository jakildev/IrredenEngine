#ifndef COMPONENT_ACTIONS_WINDOW_H
#define COMPONENT_ACTIONS_WINDOW_H

namespace IRComponents {

// Singleton: whether the Actions window is open. Flipped by the window's toggle
// action and CLOSE button; `System<ACTIONS_WINDOW>` builds and tears down the
// window's widgets on the edges, so a closed window owns no entities.
struct C_ActionsWindowState {
    bool open_ = false;
};

} // namespace IRComponents

#endif /* COMPONENT_ACTIONS_WINDOW_H */
