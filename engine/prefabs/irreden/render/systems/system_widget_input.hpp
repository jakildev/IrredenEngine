#ifndef SYSTEM_WIDGET_INPUT_H
#define SYSTEM_WIDGET_INPUT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/layout.hpp>

namespace IRSystem {

// Generic per-frame state machine for hitbox-bearing widgets. Reads
// `C_HitBox2DGui::hovered_` (populated by HITBOX_MOUSE_TEST_GUI) and the
// mouse-button state; writes `C_WidgetState`. Per-kind side effects
// (slider value update, checkbox toggle) are handled by the small
// follower systems WIDGET_APPLY_SLIDER / WIDGET_APPLY_CHECKBOX so this
// system stays free of per-entity `getComponent` calls.
//
// Pipeline order requirement: HITBOX_MOUSE_TEST_GUI → WIDGET_INPUT →
// WIDGET_APPLY_SLIDER → WIDGET_APPLY_CHECKBOX, all in the INPUT pipeline.
//
// `fireAction_` is the canonical "user just activated this widget"
// signal: it's true for the single frame in which the left mouse
// button is released while the cursor is over the widget AND the press
// originated over the same widget. Consumers poll it from any system
// that runs after WIDGET_INPUT.
//
// `dragValue_` is the slider's drag t in [0, 1] for the current frame
// when `pressed_` is set; ignored otherwise. Read by WIDGET_APPLY_SLIDER
// to interpolate `currentValue_` between min and max.
template <> struct System<WIDGET_INPUT> {
    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);
    bool mouseLeftDown_ = false;
    bool mouseLeftPressedThisFrame_ = false;
    bool mouseLeftReleasedThisFrame_ = false;

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
    }

    void tick(
        const IRComponents::C_Widget &widget,
        IRComponents::C_WidgetState &state,
        const IRComponents::C_HitBox2DGui &hitbox,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        state.fireAction_ = false;

        if (widget.disabled_) {
            state.hovered_ = false;
            state.pressed_ = false;
            return;
        }

        state.hovered_ = hitbox.hovered_;

        if (state.pressed_) {
            if (widget.kind_ == IRComponents::WidgetKind::SLIDER && widget.size_.x > 0) {
                const float dx = mouseGuiTrixel_.x - static_cast<float>(guiPos.pos_.x);
                state.dragValue_ =
                    IRMath::clamp(dx / static_cast<float>(widget.size_.x), 0.0f, 1.0f);
            }
            if (mouseLeftReleasedThisFrame_) {
                state.pressed_ = false;
                if (state.hovered_) state.fireAction_ = true;
            } else if (!mouseLeftDown_) {
                state.pressed_ = false;
            }
        } else if (state.hovered_ && mouseLeftPressedThisFrame_) {
            state.pressed_ = true;
            if (widget.kind_ == IRComponents::WidgetKind::SLIDER && widget.size_.x > 0) {
                const float dx = mouseGuiTrixel_.x - static_cast<float>(guiPos.pos_.x);
                state.dragValue_ =
                    IRMath::clamp(dx / static_cast<float>(widget.size_.x), 0.0f, 1.0f);
            }
        }
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_INPUT,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_HitBox2DGui,
            IRComponents::C_GuiPosition
        >("WidgetInput");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_INPUT_H */
