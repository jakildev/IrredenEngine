#ifndef SYSTEM_WIDGET_APPLY_LIST_H
#define SYSTEM_WIDGET_APPLY_LIST_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/input/components/component_mouse_scroll.hpp>
#include <irreden/render/layout.hpp>

namespace IRSystem {

// Per-kind follower for lists. Three responsibilities:
//   1. Scroll the hovered list one row per mouse-wheel event, and write the
//      clamped offset back so `scrollOffset_` never points past the content.
//   2. Cache the cursor's visible-row index in state.dragValue_ so
//      WIDGET_RENDER_LIST can paint a hover band on the matching row.
//   3. On fireAction, set selectedIndex_ to the clicked item. The list
//      widget's hitbox covers the entire bounds, so this system
//      reconstructs the per-row hit from the cursor's GUI-trixel Y itself.
//
// Lives separately from WIDGET_INPUT so the input system never has to
// look up C_WidgetList on a per-entity tick.
template <> struct System<WIDGET_APPLY_LIST> {
    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);
    // Rows the frame's wheel events ask for; positive scrolls toward the end.
    int wheelRows_ = 0;

    void beginTick() {
        mouseGuiTrixel_ = IRPrefab::Layout::mousePositionInGuiTrixels();
        wheelRows_ = 0;
        IREntity::forEachComponent<IRComponents::C_MouseScroll>(
            [this](IREntity::EntityId &, IRComponents::C_MouseScroll &scroll) {
                if (scroll.yoffset_ > 0.0) {
                    --wheelRows_;
                } else if (scroll.yoffset_ < 0.0) {
                    ++wheelRows_;
                }
            }
        );
    }

    void tick(
        const IRComponents::C_Widget &widget,
        IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetList &list,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        const int viewHeight = widget.size_.y;
        if (widget.disabled_) {
            list.scrollOffset_ = list.topIndex(viewHeight);
            state.dragValue_ = -1.0f;
            return;
        }

        // scrollBy clamps, so the offset is written back clamped either way.
        list.scrollBy(state.hovered_ ? wheelRows_ : 0, viewHeight);

        const int item =
            list.itemAtOffsetY(viewHeight, mouseGuiTrixel_.y - static_cast<float>(guiPos.pos_.y));
        state.dragValue_ = item >= 0 ? static_cast<float>(item - list.scrollOffset_) : -1.0f;

        if (state.fireAction_ && item >= 0) {
            list.selectedIndex_ = item;
        }
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_APPLY_LIST,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetList,
            IRComponents::C_GuiPosition,
            AlsoReads<IRComponents::C_MouseScroll>>("WidgetApplyList");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_LIST_H */
