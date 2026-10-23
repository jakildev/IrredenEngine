#ifndef SYSTEM_WIDGET_APPLY_SLIDER_H
#define SYSTEM_WIDGET_APPLY_SLIDER_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/input/components/component_mouse_scroll.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widgets.hpp>

#include <atomic>

namespace IRSystem {

// Per-kind follower for sliders. Iterates entities that carry both
// `C_WidgetState` and `C_WidgetSlider`; when the state machine left
// `pressed_` set this frame, copy the drag t into `currentValue_`
// linearly between min/max. Lives as its own system so WIDGET_INPUT
// doesn't have to call `getComponent<C_WidgetSlider>` from inside a
// per-entity tick.
template <> struct System<WIDGET_APPLY_SLIDER> {
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    IRPrefab::Widget::WidgetTheme theme_;
    int wheelSteps_ = 0;
    bool wheelShiftHeld_ = false;
    std::atomic<IREntity::EntityId> pendingFieldId_{IREntity::kNullEntity};

    void beginTick() {
        theme_ = IRPrefab::Widget::defaultTheme();
        wheelSteps_ = 0;
        IREntity::forEachComponent<IRComponents::C_MouseScroll>(
            [this](IREntity::EntityId &, IRComponents::C_MouseScroll &scroll) {
                if (scroll.yoffset_ > 0.0) {
                    ++wheelSteps_;
                } else if (scroll.yoffset_ < 0.0) {
                    --wheelSteps_;
                }
            }
        );
        wheelShiftHeld_ = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift);
        pendingFieldId_.store(IREntity::kNullEntity, std::memory_order_relaxed);
    }

    void tick(
        const IRComponents::C_Widget &widget,
        IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetSlider &slider,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (widget.disabled_) {
            return;
        }

        if (state.hovered_ && wheelSteps_ != 0) {
            IRPrefab::Widget::nudgeSliderValue(slider, wheelSteps_ * (wheelShiftHeld_ ? 10 : 1));
        }

        if (state.sliderDoubleClick_) {
            if (state.sliderFirstClickRegion_ ==
                IRComponents::C_WidgetState::SliderPressRegion::THUMB) {
                IRPrefab::Widget::resetSliderValue(slider);
            } else if (
                state.sliderFirstClickRegion_ ==
                    IRComponents::C_WidgetState::SliderPressRegion::TRACK &&
                slider.fieldId_ != IREntity::kNullEntity
            ) {
                IRPrefab::Widget::setSliderValue(slider, state.sliderFirstClickValue_);
                IREntity::EntityId expected = IREntity::kNullEntity;
                pendingFieldId_
                    .compare_exchange_strong(expected, slider.fieldId_, std::memory_order_relaxed);
            }
            state.sliderFirstClickRegion_ = IRComponents::C_WidgetState::SliderPressRegion::NONE;
            state.sliderActiveRegion_ = IRComponents::C_WidgetState::SliderPressRegion::NONE;
            state.sliderClickAge_ = 0;
            state.pressed_ = false;
            return;
        }

        if (state.sliderPressStarted_) {
            state.sliderPressValue_ = slider.currentValue_;
            state.sliderFirstClickValue_ = slider.currentValue_;
            const IRPrefab::GuiRect thumb =
                IRPrefab::Widget::sliderThumbRect(widget, slider, guiPos, theme_);
            const bool onThumb =
                state.sliderPressPosition_.x >= static_cast<float>(thumb.pos_.x) &&
                state.sliderPressPosition_.y >= static_cast<float>(thumb.pos_.y) &&
                state.sliderPressPosition_.x < static_cast<float>(thumb.pos_.x + thumb.size_.x) &&
                state.sliderPressPosition_.y < static_cast<float>(thumb.pos_.y + thumb.size_.y);
            state.sliderActiveRegion_ = onThumb
                                            ? IRComponents::C_WidgetState::SliderPressRegion::THUMB
                                            : IRComponents::C_WidgetState::SliderPressRegion::TRACK;
            state.sliderFirstClickRegion_ = state.sliderActiveRegion_;
            state.sliderClickAge_ = 1;
        }

        if (!state.pressed_) {
            return;
        }
        const float range = slider.maxValue_ - slider.minValue_;
        float value = slider.minValue_ + state.dragValue_ * range;
        if (state.sliderShiftDrag_) {
            value = state.sliderPressValue_ +
                    (state.dragValue_ - state.sliderPressFraction_) * range * 0.1f;
        }
        if (state.sliderSteppedDrag_) {
            value = IRPrefab::Widget::snapSliderValue(slider, value);
        }
        IRPrefab::Widget::setSliderValue(slider, value);
    }

    void endTick() {
        const IREntity::EntityId field =
            pendingFieldId_.exchange(IREntity::kNullEntity, std::memory_order_relaxed);
        if (field == IREntity::kNullEntity || !IREntity::entityExists(field)) {
            return;
        }
        if (!IREntity::getComponentOptional<IRComponents::C_WidgetNumberField>(field)) {
            return;
        }
        IRPrefab::Widget::focus(field);
    }

    static SystemId create() {
        IRPrefab::Widget::ensureThemeSingleton();
        IRPrefab::Widget::ensureFocusRequestSingleton();
        return registerSystem<
            WIDGET_APPLY_SLIDER,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetSlider,
            IRComponents::C_GuiPosition,
            AlsoReads<IRComponents::C_MouseScroll>,
            AlsoWrites<IRComponents::C_WidgetFocusRequest>>("WidgetApplySlider");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_SLIDER_H */
