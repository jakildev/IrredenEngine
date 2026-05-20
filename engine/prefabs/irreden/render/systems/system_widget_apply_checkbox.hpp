#ifndef SYSTEM_WIDGET_APPLY_CHECKBOX_H
#define SYSTEM_WIDGET_APPLY_CHECKBOX_H

#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_widget.hpp>

namespace IRSystem {

// Per-kind follower for checkboxes. Iterates entities that carry both
// `C_WidgetState` and `C_WidgetCheckbox`; when the state machine sets
// `fireAction_` this frame, flip `checked_`. Lives as its own system
// so WIDGET_INPUT doesn't have to call `getComponent<C_WidgetCheckbox>`
// from inside a per-entity tick.
template <> struct System<WIDGET_APPLY_CHECKBOX> {
    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetCheckbox &checkbox
    ) {
        if (state.fireAction_) checkbox.checked_ = !checkbox.checked_;
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_APPLY_CHECKBOX,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetCheckbox
        >("WidgetApplyCheckbox");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_CHECKBOX_H */
