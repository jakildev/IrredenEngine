#ifndef SYSTEM_WIDGET_APPLY_SLIDER_H
#define SYSTEM_WIDGET_APPLY_SLIDER_H

#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_widget.hpp>

namespace IRSystem {

// Per-kind follower for sliders. Iterates entities that carry both
// `C_WidgetState` and `C_WidgetSlider`; when the state machine left
// `pressed_` set this frame, copy the drag t into `currentValue_`
// linearly between min/max. Lives as its own system so WIDGET_INPUT
// doesn't have to call `getComponent<C_WidgetSlider>` from inside a
// per-entity tick.
template <> struct System<WIDGET_APPLY_SLIDER> {
    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetSlider &slider
    ) {
        if (!state.pressed_) return;
        slider.currentValue_ =
            slider.minValue_ + state.dragValue_ * (slider.maxValue_ - slider.minValue_);
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_APPLY_SLIDER,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetSlider
        >("WidgetApplySlider");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_SLIDER_H */
