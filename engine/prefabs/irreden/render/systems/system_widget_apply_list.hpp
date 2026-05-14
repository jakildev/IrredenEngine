#ifndef SYSTEM_WIDGET_APPLY_LIST_H
#define SYSTEM_WIDGET_APPLY_LIST_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/layout.hpp>

namespace IRSystem {

// Per-kind follower for lists. Two responsibilities:
//   1. Translate the cached cursor row index into state.dragValue_ so
//      WIDGET_RENDER_LIST can paint a hover band on the matching row.
//   2. On fireAction, set selectedIndex_ to the clicked row. The list
//      widget's hitbox covers the entire bounds, so this system
//      reconstructs the per-row hit by reading the cursor's GUI-trixel
//      Y position itself.
//
// Lives separately from WIDGET_INPUT so the input system never has to
// look up C_WidgetList on a per-entity tick.
template <> struct System<WIDGET_APPLY_LIST> {
    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);

    void beginTick() {
        mouseGuiTrixel_ = IRPrefab::Layout::mousePositionInGuiTrixels();
    }

    void tick(
        const IRComponents::C_Widget &widget,
        IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetList &list,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (widget.disabled_) {
            state.dragValue_ = -1.0f;
            return;
        }

        const int itemH = IRMath::max(1, list.itemHeight_);
        const int rowFromMouse = static_cast<int>(
            (mouseGuiTrixel_.y - static_cast<float>(guiPos.pos_.y)) / static_cast<float>(itemH)
        );
        const int rows = widget.size_.y / itemH;
        const bool rowInBounds = (rowFromMouse >= 0 && rowFromMouse < rows);

        // Cache the row index in dragValue_ for the render system to read.
        state.dragValue_ = rowInBounds ? static_cast<float>(rowFromMouse) : -1.0f;

        if (state.fireAction_ && rowInBounds) {
            const int n = static_cast<int>(list.items_.size());
            const int idx = list.scrollOffset_ + rowFromMouse;
            if (idx >= 0 && idx < n) {
                list.selectedIndex_ = idx;
            }
        }
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_APPLY_LIST,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetList,
            IRComponents::C_GuiPosition
        >("WidgetApplyList");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_LIST_H */
