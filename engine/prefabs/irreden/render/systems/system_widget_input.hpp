#ifndef SYSTEM_WIDGET_INPUT_H
#define SYSTEM_WIDGET_INPUT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_gui_hover_state.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/layout.hpp>
#include <irreden/render/widget_hotkeys.hpp>

#include <algorithm>
#include <climits>
#include <vector>

namespace IRSystem {

// Generic per-frame state machine for hitbox-bearing widgets. Reads
// `C_HitBox2DGui::hovered_` (populated by HITBOX_MOUSE_TEST_GUI) and the
// mouse-button state; writes `C_WidgetState`. Per-kind side effects
// (slider value update, checkbox toggle) are handled by the follower
// systems WIDGET_APPLY_SLIDER / WIDGET_APPLY_CHECKBOX.
//
// **Z-order routing**: beginTick scans all C_HitBox2DGui entities and
// picks the one with the highest `C_Widget::zOrder_` as the single
// effective hover target (`topHoveredId_`). Only that entity (or the
// captured entity during a drag) receives `state.hovered_ = true`.
//
// **Mouse capture**: on press, `capturedWidgetId_` is set to the pressed
// entity so drag events continue reaching it even when the cursor leaves
// the hitbox. Capture is released when the mouse button is released.
//
// **Keyboard focus**: a single `focusedWidgetId_` is tracked.
// Tab (forward) / Shift+Tab (backward) cycles through all non-disabled
// interactive widgets. Clicking a widget also sets focus.
//
// **Hotkey dispatch**: each `beginTick` fires registered callbacks from
// `IRPrefab::Widget::getHotkeyRegistry()` for any matching PRESSED key
// + modifier combo. Register via `IRPrefab::Widget::registerHotkey`.
//
// **Stale capture / focus on widget destruction**: `capturedWidgetId_`
// and `focusedWidgetId_` are `EntityId` values, not pointers, so they
// cannot dangle. If the entity they point to is destroyed during a
// drag or while focused, behavior is benign: the captured ID resolves
// to no matching tick body (the iterator skips dead entities), and the
// stored ID is cleared on the next mouse-button-release / Tab / click.
// Editor widgets are not destroyed mid-interaction in practice; if a
// future use case needs to destroy interactive widgets during a drag,
// add an entity-validity guard in `beginTick`.
//
// Pipeline order requirement: HITBOX_MOUSE_TEST_GUI → WIDGET_INPUT →
// WIDGET_APPLY_SLIDER → WIDGET_APPLY_CHECKBOX, all in the INPUT pipeline.
template <> struct System<WIDGET_INPUT> {
    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);
    bool mouseLeftDown_ = false;
    bool mouseLeftPressedThisFrame_ = false;
    bool mouseLeftReleasedThisFrame_ = false;

    IREntity::EntityId topHoveredId_ = IREntity::kNullEntity;
    IREntity::EntityId capturedWidgetId_ = IREntity::kNullEntity;
    IREntity::EntityId focusedWidgetId_ = IREntity::kNullEntity;

    std::vector<IREntity::EntityId> focusableWidgets_;

    void beginTick() {
        mouseGuiTrixel_ = IRPrefab::Layout::mousePositionInGuiTrixels();

        mouseLeftPressedThisFrame_ = IRInput::checkKeyMouseButton(
            IRInput::KeyMouseButtons::kMouseButtonLeft,
            IRInput::ButtonStatuses::PRESSED
        );
        mouseLeftReleasedThisFrame_ = IRInput::checkKeyMouseButton(
            IRInput::KeyMouseButtons::kMouseButtonLeft,
            IRInput::ButtonStatuses::RELEASED
        );
        const bool heldThisFrame = IRInput::checkKeyMouseButton(
            IRInput::KeyMouseButtons::kMouseButtonLeft,
            IRInput::ButtonStatuses::HELD
        );
        mouseLeftDown_ = heldThisFrame || mouseLeftPressedThisFrame_;

        // Z-order routing: captured widget holds its hover; otherwise scan for
        // the topmost (highest zOrder_) entity whose hitbox reports hovered_.
        if (capturedWidgetId_ != IREntity::kNullEntity) {
            topHoveredId_ = capturedWidgetId_;
        } else {
            topHoveredId_ = IREntity::kNullEntity;
            int topZ = INT_MIN;
            IREntity::forEachComponent<IRComponents::C_HitBox2DGui>(
                [this, &topZ](IREntity::EntityId &id, IRComponents::C_HitBox2DGui &hitbox) {
                    if (!hitbox.hovered_)
                        return;
                    const auto &widget = IREntity::getComponent<IRComponents::C_Widget>(id);
                    if (!widget.disabled_ && widget.zOrder_ >= topZ) {
                        topZ = widget.zOrder_;
                        topHoveredId_ = id;
                    }
                }
            );
        }

        // Release capture when the mouse button comes up.
        if (mouseLeftReleasedThisFrame_) {
            capturedWidgetId_ = IREntity::kNullEntity;
        }

        // Tab / Shift+Tab: cycle focus through all non-disabled interactive widgets.
        const bool tabPressed = IRInput::checkKeyMouseButton(
            IRInput::KeyMouseButtons::kKeyButtonTab,
            IRInput::ButtonStatuses::PRESSED
        );
        if (tabPressed) {
            const bool shiftHeld = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift);
            focusableWidgets_.clear();
            IREntity::forEachComponent<IRComponents::C_HitBox2DGui>(
                [this](IREntity::EntityId &id, IRComponents::C_HitBox2DGui &) {
                    const auto &widget = IREntity::getComponent<IRComponents::C_Widget>(id);
                    if (!widget.disabled_) {
                        focusableWidgets_.push_back(id);
                    }
                }
            );
            if (!focusableWidgets_.empty()) {
                const int n = static_cast<int>(focusableWidgets_.size());
                const int step = shiftHeld ? -1 : 1;
                auto it =
                    std::find(focusableWidgets_.begin(), focusableWidgets_.end(), focusedWidgetId_);
                if (it == focusableWidgets_.end()) {
                    focusedWidgetId_ = focusableWidgets_[0];
                } else {
                    const int idx =
                        (static_cast<int>(it - focusableWidgets_.begin()) + step + n) % n;
                    focusedWidgetId_ = focusableWidgets_[static_cast<size_t>(idx)];
                }
            }
        }

        // Hotkey dispatch: fire registered callbacks for matching PRESSED combos.
        for (auto &[key, entry] : IRPrefab::Widget::getHotkeyRegistry()) {
            const IRInput::KeyModifierMask mods = static_cast<IRInput::KeyModifierMask>(key >> 16);
            const IRInput::KeyMouseButtons button =
                static_cast<IRInput::KeyMouseButtons>(key & 0xFFFFu);
            if (IRInput::checkKeyMouseButton(button, IRInput::ButtonStatuses::PRESSED) &&
                IRInput::checkKeyMouseModifiers(mods)) {
                entry.callback_();
            }
        }
    }

    void tick(
        IREntity::EntityId entityId,
        const IRComponents::C_Widget &widget,
        IRComponents::C_WidgetState &state,
        const IRComponents::C_HitBox2DGui &hitbox,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        state.fireAction_ = false;
        state.focused_ = (entityId == focusedWidgetId_);

        if (widget.disabled_) {
            state.hovered_ = false;
            state.pressed_ = false;
            return;
        }

        // Captured widget stays effectively hovered even when the cursor leaves its
        // hitbox so drag operations (sliders) continue until button release.
        const bool effectivelyHovered =
            (entityId == capturedWidgetId_) || (hitbox.hovered_ && entityId == topHoveredId_);
        state.hovered_ = effectivelyHovered;

        if (state.pressed_) {
            if (widget.kind_ == IRComponents::WidgetKind::SLIDER && widget.size_.x > 0) {
                const float dx = mouseGuiTrixel_.x - static_cast<float>(guiPos.pos_.x);
                state.dragValue_ =
                    IRMath::clamp(dx / static_cast<float>(widget.size_.x), 0.0f, 1.0f);
            }
            if (mouseLeftReleasedThisFrame_) {
                state.pressed_ = false;
                if (effectivelyHovered)
                    state.fireAction_ = true;
            } else if (!mouseLeftDown_) {
                state.pressed_ = false;
            }
        } else if (effectivelyHovered && mouseLeftPressedThisFrame_) {
            state.pressed_ = true;
            capturedWidgetId_ = entityId;
            focusedWidgetId_ = entityId;
            if (widget.kind_ == IRComponents::WidgetKind::SLIDER && widget.size_.x > 0) {
                const float dx = mouseGuiTrixel_.x - static_cast<float>(guiPos.pos_.x);
                state.dragValue_ =
                    IRMath::clamp(dx / static_cast<float>(widget.size_.x), 0.0f, 1.0f);
            }
        }
    }

    // Publish the z-ordered topmost hovered widget (resolved in beginTick)
    // into the C_GuiHoverState singleton, if a creation registered one. A
    // once-per-frame singleton write (endTick is allowed to do bounded
    // lookups, unlike the per-entity tick). No singleton → no-op, so
    // non-test creations pay nothing. Read back via
    // IRPrefab::Widget::hoveredWidget(); used by the headless GUI-test
    // harness (#1796) to assert hover without re-scanning hitboxes.
    void endTick() {
        IREntity::forEachComponent<IRComponents::C_GuiHoverState>(
            [this](IREntity::EntityId &, IRComponents::C_GuiHoverState &hoverState) {
                hoverState.hoveredWidget_ = topHoveredId_;
            }
        );
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_INPUT,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_HitBox2DGui,
            IRComponents::C_GuiPosition>("WidgetInput");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_INPUT_H */
