#ifndef SYSTEM_WIDGET_APPLY_SCROLL_H
#define SYSTEM_WIDGET_APPLY_SCROLL_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/layout.hpp>

namespace IRSystem {

// Per-kind follower for scroll bars. Translates a drag (or a click on
// the track) into a scroll position along the widget's axis.
// `state.pressed_` is held by WIDGET_INPUT for as long as the user is
// dragging; the apply system reads the cursor's per-axis offset against
// the widget bounds and maps it linearly into [0, contentSize - view].
template <> struct System<WIDGET_APPLY_SCROLL> {
    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);

    void beginTick() {
        mouseGuiTrixel_ = IRPrefab::Layout::mousePositionInGuiTrixels();
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetScroll &scroll,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (widget.disabled_) return;
        if (!state.pressed_) return;
        if (widget.size_.x <= 0 || widget.size_.y <= 0) return;

        const bool vertical = (scroll.axis_ == IRComponents::C_WidgetScroll::Axis::VERTICAL);
        const int viewExtent = vertical ? widget.size_.y : widget.size_.x;
        const int contentExtent = IRMath::max(viewExtent, scroll.contentSize_);
        const int maxScroll = IRMath::max(0, contentExtent - viewExtent);
        if (maxScroll == 0) {
            scroll.scrollPos_ = 0;
            return;
        }

        const float rel = vertical
            ? (mouseGuiTrixel_.y - static_cast<float>(guiPos.pos_.y))
            : (mouseGuiTrixel_.x - static_cast<float>(guiPos.pos_.x));
        const float t = IRMath::clamp(rel / static_cast<float>(viewExtent), 0.0f, 1.0f);
        scroll.scrollPos_ = static_cast<int>(t * static_cast<float>(maxScroll) + 0.5f);
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_APPLY_SCROLL,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetScroll,
            IRComponents::C_GuiPosition
        >("WidgetApplyScroll");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_SCROLL_H */
