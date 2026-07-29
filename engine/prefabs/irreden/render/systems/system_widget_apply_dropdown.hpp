#ifndef SYSTEM_WIDGET_APPLY_DROPDOWN_H
#define SYSTEM_WIDGET_APPLY_DROPDOWN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/layout.hpp>

namespace IRSystem {

// Z-order bias applied to a dropdown for as long as it is expanded, so its item
// strip out-ranks whatever it overlaps in WIDGET_INPUT's hover routing. The
// input-side counterpart to registering WIDGET_RENDER_DROPDOWN last in the
// render chain: the strip is *painted* over its neighbors either way, but
// without this it loses the equal-zOrder tie-break to the widgets underneath and
// every item row that covers one is unclickable (the settings menu's ENUM rows
// are the reference case — a dropdown there always overlaps the row below it).
// Biased rather than assigned so an authored z-order survives the round trip;
// authored values must stay below the bias.
inline constexpr int kWidgetDropdownOpenZBias = 1000;

// Per-kind follower for dropdowns. Owns four pieces of behavior:
//   1. While open, grows the hitbox to cover the expanded item panel and
//      biases zOrder_ so WIDGET_INPUT's hover routing reaches this widget
//      rather than whatever the panel covers.
//   2. Caches the cursor's row index in state.dragValue_ for the render
//      system to highlight, mirroring the list pattern.
//   3. Handles click semantics — first click opens the dropdown; a
//      second click on a row selects that row and closes; a click on
//      the header while open closes without selection; a click outside
//      while open also closes (handled via a press-released-outside
//      check on the global mouse-button state).
template <> struct System<WIDGET_APPLY_DROPDOWN> {
    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);

    void beginTick() {
        mouseGuiTrixel_ = IRPrefab::Layout::mousePositionInGuiTrixels();
    }

    // Bias zOrder_ up while expanded and back down when collapsed. Derived from
    // the current value rather than a stored base, so it is idempotent across
    // frames and leaves an authored z-order intact.
    static void syncFloatingZOrder(IRComponents::C_Widget &widget, bool isOpen) {
        const bool floating = widget.zOrder_ >= kWidgetDropdownOpenZBias;
        if (floating != isOpen) {
            widget.zOrder_ += isOpen ? kWidgetDropdownOpenZBias : -kWidgetDropdownOpenZBias;
        }
    }

    void tick(
        IRComponents::C_Widget &widget,
        IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetDropdown &dd,
        IRComponents::C_HitBox2DGui &hitbox,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (widget.disabled_) {
            dd.isOpen_ = false;
            hitbox.size_ = widget.size_;
            syncFloatingZOrder(widget, false);
            state.dragValue_ = -1.0f;
            return;
        }

        const int itemH = IRMath::max(1, dd.itemHeight_);
        const int n = static_cast<int>(dd.items_.size());

        // Sync hitbox extent and z-order to current open state so WIDGET_INPUT's
        // hover routing can find us anywhere inside the expanded panel, ahead of
        // the widgets that panel covers.
        const int openHeight = widget.size_.y + (n > 0 ? itemH * n : 0);
        hitbox.size_ = IRMath::ivec2(widget.size_.x, dd.isOpen_ ? openHeight : widget.size_.y);
        syncFloatingZOrder(widget, dd.isOpen_);

        // Compute the row the cursor is currently over, if any. -1 = not
        // over a row (cursor on header, outside widget, or list empty).
        int rowFromMouse = -1;
        if (dd.isOpen_ && n > 0) {
            const float rel =
                mouseGuiTrixel_.y - static_cast<float>(guiPos.pos_.y + widget.size_.y);
            const float withinX = mouseGuiTrixel_.x - static_cast<float>(guiPos.pos_.x);
            if (rel >= 0.0f && withinX >= 0.0f && withinX < static_cast<float>(widget.size_.x)) {
                const int r = static_cast<int>(rel / static_cast<float>(itemH));
                if (r >= 0 && r < n)
                    rowFromMouse = r;
            }
        }
        state.dragValue_ = static_cast<float>(rowFromMouse);

        if (!state.fireAction_)
            return;

        if (!dd.isOpen_) {
            dd.isOpen_ = true;
            return;
        }

        // Open + clicked: if a row was hovered at release, select it.
        if (rowFromMouse >= 0 && rowFromMouse < n) {
            dd.selectedIndex_ = rowFromMouse;
        }
        dd.isOpen_ = false;
        hitbox.size_ = widget.size_;
        syncFloatingZOrder(widget, false);
        // TODO: outside-click-to-close — close if mouse released outside the
        // expanded hitbox; needs a direct mouse-released check rather than
        // state.fireAction_ (which requires the click to land inside the widget).
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_APPLY_DROPDOWN,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetDropdown,
            IRComponents::C_HitBox2DGui,
            IRComponents::C_GuiPosition>("WidgetApplyDropdown");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_DROPDOWN_H */
