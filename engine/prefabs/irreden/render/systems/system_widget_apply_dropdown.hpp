#ifndef SYSTEM_WIDGET_APPLY_DROPDOWN_H
#define SYSTEM_WIDGET_APPLY_DROPDOWN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_widget_input_frame.hpp>
#include <irreden/input/components/component_mouse_scroll.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/widget_text_layout.hpp>
#include <irreden/render/widget_theme.hpp>

namespace IRSystem {

// The z-order bias below is `IRComponents::kWidgetDropdownOpenZBias`, declared
// beside `C_Widget::zOrder_` (the field it constrains) so the widget ctor can
// assert authored values stay under it. Applied here for as long as a dropdown
// is expanded, so its item strip out-ranks whatever it overlaps in
// WIDGET_INPUT's hover routing. The input-side counterpart to registering
// WIDGET_RENDER_DROPDOWN last in the render chain: the strip is *painted* over
// its neighbors either way, but without this it loses the equal-zOrder
// tie-break to the widgets underneath and every item row that covers one is
// unclickable (the settings menu's ENUM rows are the reference case — a
// dropdown there always overlaps the row below it). Biased rather than assigned
// so an authored z-order survives the round trip.
//
// **The bias does not order two dropdowns against each other.** Nothing closes
// a dropdown when another opens (see the outside-click-to-close TODO below), so
// two *simultaneously* expanded dropdowns both sit at base+bias and the
// equal-zOrder tie-break this exists to break comes back for that pair — the
// upper strip can lose to the lower dropdown it covers. Reachable in the
// settings menu (expand DEBUG OVERLAY, then ROTATION PIVOT above it). Ordering
// them needs an open-order rank, not a flat bias.

// Per-kind follower for dropdowns. Owns five pieces of behavior:
//   1. While open, grows the hitbox over the header and the strip where
//      `C_WidgetDropdown::stripLayout` places it against the GUI canvas
//      (offset upward for a strip above the header), and biases zOrder_ so
//      WIDGET_INPUT's hover routing reaches this widget rather than whatever
//      the strip covers.
//   2. Scrolls the open item window one row per mouse-wheel event while the
//      dropdown is the topmost hovered widget, reveals the selected item when
//      the strip opens, and keeps the keyboard row inside the window.
//   3. Caches the cursor's absolute item index in state.dragValue_ for the
//      render system to highlight.
//   4. Handles click semantics — first click opens the dropdown; a
//      second click on a row selects that row and closes; a click on
//      the header while open closes without selection; a click outside
//      while open also closes (handled via a press-released-outside
//      check on the global mouse-button state).
template <> struct System<WIDGET_APPLY_DROPDOWN> {
    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);
    // The GUI canvas height every strip of the frame is placed against.
    int guiCanvasHeight_ = 0;
    // Rows the frame's wheel events ask for; positive scrolls toward the end.
    int wheelRows_ = 0;
    IREntity::EntityId keyboardDropdown_ = IREntity::kNullEntity;
    int keyboardRow_ = -1;
    IREntity::EntityId focusedWidget_ = IREntity::kNullEntity;
    std::string_view typedText_;
    bool keyboardOwnsHighlight_ = false;
    IRMath::vec2 keyboardMousePosition_ = IRMath::vec2(0.0f);

    static int keyboardStartRow(const IRComponents::C_WidgetDropdown &dropdown) {
        if (dropdown.shownItemCount() == 0) {
            return -1;
        }
        if (dropdown.itemShown(dropdown.selectedIndex_)) {
            return dropdown.selectedIndex_;
        }
        const IRComponents::C_WidgetDropdown::StripLayout allRows{0, dropdown.shownItemCount()};
        return dropdown.shownItemAt(allRows, 0);
    }

    static int firstShownRow(const IRComponents::C_WidgetDropdown &dropdown) {
        const IRComponents::C_WidgetDropdown::StripLayout allRows{0, dropdown.shownItemCount()};
        return dropdown.shownItemAt(allRows, 0);
    }

    void ensureKeyboardCursor(
        IREntity::EntityId entityId, const IRComponents::C_WidgetDropdown &dropdown
    ) {
        if (keyboardDropdown_ != entityId || !dropdown.itemShown(keyboardRow_)) {
            keyboardDropdown_ = entityId;
            keyboardRow_ = keyboardStartRow(dropdown);
        }
    }

    void clearKeyboardCursor(IREntity::EntityId entityId) {
        if (keyboardDropdown_ == entityId) {
            keyboardDropdown_ = IREntity::kNullEntity;
            keyboardRow_ = -1;
        }
    }

    void beginTick() {
        const auto &frame = IREntity::singleton<IRComponents::C_WidgetInputFrame>();
        mouseGuiTrixel_ = frame.mouseGuiTrixel_;
        guiCanvasHeight_ = frame.guiCanvasSize_.y;
        focusedWidget_ = frame.focusedWidget_;
        typedText_ = frame.typedText_;
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

    static void closeDropdown(
        IREntity::EntityId entityId,
        IRComponents::C_WidgetDropdown &dropdown,
        System<WIDGET_APPLY_DROPDOWN> &system
    ) {
        dropdown.isOpen_ = false;
        dropdown.typeahead_.clear();
        system.clearKeyboardCursor(entityId);
        system.keyboardOwnsHighlight_ = false;
    }

    static bool queryFits(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetDropdown &dropdown,
        std::size_t queryLength
    ) {
        if (queryLength > IRComponents::C_WidgetDropdown::kMaxTypeaheadCharacters) {
            return false;
        }
        const auto &theme = IRPrefab::Widget::defaultTheme();
        const int regionWidth =
            IRPrefab::WidgetText::dropdownHeaderTextRegion(widget, IRMath::ivec2(0), theme)
                .size_.x -
            IRPrefab::Widget::detail::dropdownHorizontalInset(theme.padding_);
        const int fontSize = IRPrefab::Widget::detail::kWidgetTextFontSize;
        const int textWidth =
            queryLength == 0 ? 0
                             : static_cast<int>(queryLength) *
                                       IRPrefab::GuiText::glyphStepX(fontSize, dropdown.textSize_) -
                                   IRPrefab::GuiText::glyphSpacingX(fontSize, dropdown.textSize_);
        return textWidth <= IRMath::max(0, regionWidth);
    }

    static void
    collapseHitbox(IRComponents::C_HitBox2DGui &hitbox, const IRComponents::C_Widget &widget) {
        hitbox.offset_ = IRMath::ivec2(0);
        hitbox.size_ = widget.size_;
    }

    // Bias zOrder_ up while expanded and back down when collapsed. Derived from
    // the current value rather than a stored base, so it is idempotent across
    // frames and leaves an authored z-order intact.
    static void syncFloatingZOrder(IRComponents::C_Widget &widget, bool isOpen) {
        const bool floating = widget.zOrder_ >= IRComponents::kWidgetDropdownOpenZBias;
        if (floating != isOpen) {
            widget.zOrder_ += isOpen ? IRComponents::kWidgetDropdownOpenZBias
                                     : -IRComponents::kWidgetDropdownOpenZBias;
        }
    }

    void tick(
        IREntity::EntityId entityId,
        IRComponents::C_Widget &widget,
        IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetDropdown &dd,
        IRComponents::C_HitBox2DGui &hitbox,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (widget.disabled_) {
            closeDropdown(entityId, dd, *this);
            collapseHitbox(hitbox, widget);
            syncFloatingZOrder(widget, false);
            state.dragValue_ = -1.0f;
            return;
        }

        if (focusedWidget_ != entityId) {
            dd.typeahead_.clear();
        }

        using Action = IRComponents::C_WidgetState::KeyAction;
        bool edited = false;
        if (focusedWidget_ == entityId && !typedText_.empty()) {
            for (const char character : typedText_) {
                if (queryFits(widget, dd, dd.typeahead_.size() + 1)) {
                    dd.typeahead_.push_back(character);
                    edited = true;
                }
            }
            if (edited) {
                dd.isOpen_ = true;
            }
        }
        if (dd.isOpen_ && state.keyAction_ == Action::ERASE && !dd.typeahead_.empty()) {
            dd.typeahead_.pop_back();
            edited = true;
        }
        if (dd.isOpen_ && state.keyAction_ == Action::CANCEL) {
            if (!dd.typeahead_.empty()) {
                dd.typeahead_.clear();
                edited = true;
            } else {
                closeDropdown(entityId, dd, *this);
            }
        }

        if (!dd.isOpen_) {
            dd.typeahead_.clear();
        }

        // One placement for the whole tick: the wheel, the keyboard row, the
        // hitbox and the hover row all read this window.
        IRComponents::C_WidgetDropdown::StripLayout layout =
            dd.stripLayout(guiPos.pos_.y, widget.size_.y, guiCanvasHeight_);

        if (edited) {
            keyboardDropdown_ = entityId;
            if (!dd.typeahead_.empty()) {
                dd.scrollOffset_ = 0;
                keyboardRow_ = firstShownRow(dd);
            } else {
                ensureKeyboardCursor(entityId, dd);
                dd.scrollIntoView(keyboardRow_, layout);
            }
            keyboardOwnsHighlight_ = true;
            keyboardMousePosition_ = mouseGuiTrixel_;
        }

        // Scroll before the hover row is resolved so the highlight and the
        // painted rows come from the same window.
        if (dd.isOpen_ && state.hovered_) {
            dd.scrollBy(wheelRows_, layout);
            if (wheelRows_ != 0) {
                keyboardOwnsHighlight_ = false;
            }
        }

        if (state.keyAction_ == Action::OPEN_OR_CLOSE) {
            dd.isOpen_ = !dd.isOpen_;
            if (dd.isOpen_) {
                keyboardDropdown_ = entityId;
                keyboardRow_ = keyboardStartRow(dd);
                dd.scrollIntoView(keyboardRow_, layout);
            } else {
                closeDropdown(entityId, dd, *this);
            }
        } else if (
            dd.isOpen_ &&
            (state.keyAction_ == Action::MOVE_UP || state.keyAction_ == Action::MOVE_DOWN)
        ) {
            ensureKeyboardCursor(entityId, dd);
            if (keyboardRow_ >= 0) {
                const int ordinal = dd.shownOrdinalOf(keyboardRow_);
                const int delta = state.keyAction_ == Action::MOVE_UP ? -1 : 1;
                const int target = IRMath::clamp(ordinal + delta, 0, dd.shownItemCount() - 1);
                const IRComponents::C_WidgetDropdown::StripLayout allRows{0, dd.shownItemCount()};
                keyboardRow_ = dd.shownItemAt(allRows, target);
                dd.scrollIntoView(keyboardRow_, layout);
                keyboardOwnsHighlight_ = true;
                keyboardMousePosition_ = mouseGuiTrixel_;
            }
        } else if (dd.isOpen_ && state.keyAction_ == Action::SELECT) {
            ensureKeyboardCursor(entityId, dd);
            int pointerRow = -1;
            const float withinX = mouseGuiTrixel_.x - static_cast<float>(guiPos.pos_.x);
            if (withinX >= 0.0f && withinX < static_cast<float>(widget.size_.x)) {
                pointerRow =
                    dd.itemAtOffsetY(layout, mouseGuiTrixel_.y - static_cast<float>(guiPos.pos_.y));
            }
            const int selected =
                !keyboardOwnsHighlight_ && pointerRow >= 0 ? pointerRow : keyboardRow_;
            if (selected >= 0) {
                dd.selectedIndex_ = selected;
                closeDropdown(entityId, dd, *this);
            }
        }

        // Sync hitbox extent and z-order to current open state so WIDGET_INPUT's
        // hover routing can find us anywhere inside the expanded panel, ahead of
        // the widgets that panel covers.
        if (dd.isOpen_) {
            hitbox.offset_ = IRMath::ivec2(0, dd.expandedOffsetY(layout));
            hitbox.size_ = IRMath::ivec2(widget.size_.x, dd.expandedHeight(widget.size_.y, layout));
        } else {
            collapseHitbox(hitbox, widget);
        }
        syncFloatingZOrder(widget, dd.isOpen_);

        // Compute the row the cursor is currently over, if any. -1 = not
        // over a row (cursor on header, outside widget, or list empty).
        int rowFromMouse = -1;
        if (dd.isOpen_) {
            const float withinX = mouseGuiTrixel_.x - static_cast<float>(guiPos.pos_.x);
            if (withinX >= 0.0f && withinX < static_cast<float>(widget.size_.x)) {
                rowFromMouse =
                    dd.itemAtOffsetY(layout, mouseGuiTrixel_.y - static_cast<float>(guiPos.pos_.y));
            }
        }
        if (keyboardOwnsHighlight_ && (mouseGuiTrixel_.x != keyboardMousePosition_.x ||
                                       mouseGuiTrixel_.y != keyboardMousePosition_.y)) {
            keyboardOwnsHighlight_ = false;
        }
        state.dragValue_ = static_cast<float>(
            !keyboardOwnsHighlight_ && rowFromMouse >= 0
                ? rowFromMouse
                : (keyboardDropdown_ == entityId ? keyboardRow_ : -1)
        );

        if (state.keyAction_ != Action::NONE) {
            return;
        }

        if (!state.fireAction_)
            return;

        if (!dd.isOpen_) {
            dd.isOpen_ = true;
            clearKeyboardCursor(entityId);
            dd.scrollIntoView(dd.selectedIndex_, layout);
            return;
        }

        // Open + clicked: if a row was hovered at release, select it.
        // `itemAtOffsetY` already bounded the index, so >= 0 is the whole test.
        if (rowFromMouse >= 0) {
            dd.selectedIndex_ = rowFromMouse;
        }
        closeDropdown(entityId, dd, *this);
        collapseHitbox(hitbox, widget);
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
            IRComponents::C_GuiPosition,
            AlsoReads<IRComponents::C_MouseScroll>>("WidgetApplyDropdown");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_DROPDOWN_H */
