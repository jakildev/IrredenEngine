#ifndef SYSTEM_WIDGET_INPUT_H
#define SYSTEM_WIDGET_INPUT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_command.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_gui_hover_state.hpp>
#include <irreden/render/components/component_widget_hotkey.hpp>
#include <irreden/render/components/component_widget_input_frame.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/layout.hpp>
#include <irreden/render/widget_hotkeys.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widgets.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
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
// Tab (forward) / Shift+Tab (backward) cycles through non-disabled widgets
// that do not capture keyboard editing. Text inputs and number fields gain
// focus by pointer click or by `IRPrefab::Widget::focus()`, and release it on
// Tab without transferring focus on that press. A `focus()` request lands at
// the start of the next beginTick, so the key that asked for it never reaches
// the newly focused editor.
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
// Editor widgets must not be destroyed mid-interaction; doing so requires an
// entity-validity guard in `beginTick`.
//
// Pipeline order requirement: HITBOX_MOUSE_TEST_GUI → WIDGET_INPUT →
// WIDGET_APPLY_SLIDER → WIDGET_APPLY_CHECKBOX, all in the INPUT pipeline.
template <> struct System<WIDGET_INPUT> {
    static constexpr std::array kSliderCaptured{
        IRInput::kKeyButtonUp,
        IRInput::kKeyButtonDown,
        IRInput::kKeyButtonHome,
        IRInput::kKeyButtonEnd,
        IRInput::kKeyButtonDelete,
        IRInput::kKeyButtonBackspace,
    };
    static constexpr std::array kToggleCaptured{IRInput::kKeyButtonSpace};
    static constexpr std::array kPressCaptured{IRInput::kKeyButtonEnter};
    static constexpr std::array kClosedDropdownCaptured{IRInput::kKeyButtonSpace};
    static constexpr std::array kOpenDropdownCaptured{
        IRInput::kKeyButtonSpace,
        IRInput::kKeyButtonUp,
        IRInput::kKeyButtonDown,
        IRInput::kKeyButtonEnter,
    };

    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);
    bool mouseLeftDown_ = false;
    bool mouseLeftPressedThisFrame_ = false;
    bool mouseLeftReleasedThisFrame_ = false;
    bool shiftHeld_ = false;
    bool controlHeld_ = false;
    IRInput::KeyModifierMask heldModifiers_ = IRInput::kModifierNone;

    IREntity::EntityId topHoveredId_ = IREntity::kNullEntity;
    IREntity::EntityId capturedWidgetId_ = IREntity::kNullEntity;
    IREntity::EntityId focusedWidgetId_ = IREntity::kNullEntity;
    IREntity::EntityId hotkeyWidgetId_ = IREntity::kNullEntity;
    bool hotkeyActivates_ = false;
    bool keyboardWidgetFocusedAtStart_ = false;
    bool focusVisible_ = false;
    bool focusVisibleAtStart_ = false;

    IRMath::vec2 (*mousePosition_)() = IRPrefab::Layout::mousePositionInGuiTrixels;

    std::vector<IREntity::EntityId> focusableWidgets_;
    std::vector<std::function<void()>> hotkeyCallbacks_;

    static void clearSliderPress(IRComponents::C_WidgetState &state) {
        state.sliderPressPosition_ = IRMath::vec2(0.0f);
        state.sliderPressFraction_ = 0.0f;
        state.sliderPressValue_ = 0.0f;
        state.sliderShiftDrag_ = false;
        state.sliderSteppedDrag_ = false;
        state.sliderActiveRegion_ = IRComponents::C_WidgetState::SliderPressRegion::NONE;
    }

    bool hasFocusedKeyboardWidget() const {
        if (focusedWidgetId_ == IREntity::kNullEntity ||
            !IREntity::entityExists(focusedWidgetId_)) {
            return false;
        }
        auto widget = IREntity::getComponentOptional<IRComponents::C_Widget>(focusedWidgetId_);
        return widget.has_value() && IRPrefab::Widget::takesKeyboardFocus(**widget);
    }

    static bool pressed(IRInput::KeyMouseButtons key) {
        return IRInput::checkKeyMouseButton(key, IRInput::PRESSED);
    }

    void publishFrame() const {
        auto &frame = IREntity::singleton<IRComponents::C_WidgetInputFrame>();
        frame.focusedWidget_ = focusedWidgetId_;
        frame.focusVisible_ = focusVisible_;
        frame.mouseGuiTrixel_ = mouseGuiTrixel_;
        const auto &theme = IRPrefab::Widget::defaultTheme();
        frame.hintsVisible_ = theme.hintModifier_ != IRInput::kModifierNone &&
                              (heldModifiers_ & theme.hintModifier_) == theme.hintModifier_;
    }

    void beginTick() {
        const IREntity::EntityId requested = IRPrefab::Widget::takeFocusRequest();
        if (requested != IREntity::kNullEntity && IREntity::entityExists(requested)) {
            focusedWidgetId_ = requested;
            focusVisible_ = true;
        }
        keyboardWidgetFocusedAtStart_ = hasFocusedKeyboardWidget();
        focusVisibleAtStart_ = focusVisible_;
        mouseGuiTrixel_ = mousePosition_();
        heldModifiers_ = IRInput::heldKeyModifiers();
        hotkeyWidgetId_ = IREntity::kNullEntity;
        hotkeyActivates_ = false;

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
        shiftHeld_ = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift);
        controlHeld_ = IRInput::checkKeyMouseModifiers(IRInput::kModifierControl);

        // Z-order routing: the captured widget holds its hover; otherwise the
        // topmost hovered widget takes it.
        topHoveredId_ = capturedWidgetId_ != IREntity::kNullEntity
                            ? capturedWidgetId_
                            : IRPrefab::Widget::topmostHoveredWidget();

        // Release capture when the mouse button comes up.
        if (mouseLeftReleasedThisFrame_) {
            capturedWidgetId_ = IREntity::kNullEntity;
        }

        // Focus ends with its widget, with a click that lands on no widget, and
        // when the focused keyboard editor submits or cancels its edit.
        if (focusedWidgetId_ != IREntity::kNullEntity) {
            const auto focusedWidget =
                IREntity::entityExists(focusedWidgetId_)
                    ? IREntity::getComponentOptional<IRComponents::C_Widget>(focusedWidgetId_)
                    : std::nullopt;
            const bool submitted = focusedWidget &&
                                   IRPrefab::Widget::takesKeyboardFocus(**focusedWidget) &&
                                   IRInput::checkKeyMouseButton(
                                       IRInput::KeyMouseButtons::kKeyButtonEnter,
                                       IRInput::ButtonStatuses::PRESSED
                                   );
            const bool clickedAway =
                mouseLeftPressedThisFrame_ && topHoveredId_ == IREntity::kNullEntity;
            const bool cancelled =
                focusedWidget &&
                (*focusedWidget)->kind_ == IRComponents::WidgetKind::NUMBER_FIELD &&
                IRInput::checkKeyMouseButton(
                    IRInput::KeyMouseButtons::kKeyButtonEscape,
                    IRInput::ButtonStatuses::PRESSED
                );
            if (!focusedWidget || (*focusedWidget)->disabled_ || submitted || cancelled ||
                clickedAway) {
                focusedWidgetId_ = IREntity::kNullEntity;
                focusVisible_ = false;
            }
        }

        // Tab / Shift+Tab: cycle focus through all non-disabled interactive widgets.
        const bool tabPressed = IRInput::checkKeyMouseButton(
            IRInput::KeyMouseButtons::kKeyButtonTab,
            IRInput::ButtonStatuses::PRESSED
        );
        if (tabPressed) {
            if (keyboardWidgetFocusedAtStart_) {
                focusedWidgetId_ = IREntity::kNullEntity;
                focusVisible_ = false;
            } else if (!IRPrefab::Widget::commandClaimsPress(
                           IRInput::kKeyButtonTab,
                           heldModifiers_
                       )) {
                const bool shiftHeld = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift);
                focusableWidgets_.clear();
                IREntity::forEachComponent<IRComponents::C_HitBox2DGui>(
                    [this](IREntity::EntityId &id, IRComponents::C_HitBox2DGui &) {
                        const auto &widget = IREntity::getComponent<IRComponents::C_Widget>(id);
                        if (IRPrefab::Widget::isTabFocusCandidate(widget)) {
                            focusableWidgets_.push_back(id);
                        }
                    }
                );
                std::sort(
                    focusableWidgets_.begin(),
                    focusableWidgets_.end(),
                    [](IREntity::EntityId lhs, IREntity::EntityId rhs) {
                        const auto &a =
                            IREntity::getComponent<IRComponents::C_GuiPosition>(lhs).pos_;
                        const auto &b =
                            IREntity::getComponent<IRComponents::C_GuiPosition>(rhs).pos_;
                        return a.y != b.y ? a.y < b.y : (a.x != b.x ? a.x < b.x : lhs < rhs);
                    }
                );
                if (!focusableWidgets_.empty()) {
                    const int n = static_cast<int>(focusableWidgets_.size());
                    const int step = shiftHeld ? -1 : 1;
                    auto it = std::find(
                        focusableWidgets_.begin(),
                        focusableWidgets_.end(),
                        focusedWidgetId_
                    );
                    if (it == focusableWidgets_.end()) {
                        focusedWidgetId_ = focusableWidgets_[0];
                    } else {
                        const int idx =
                            (static_cast<int>(it - focusableWidgets_.begin()) + step + n) % n;
                        focusedWidgetId_ = focusableWidgets_[static_cast<size_t>(idx)];
                    }
                    focusVisible_ = true;
                }
            }
        }

        if (!hasFocusedKeyboardWidget()) {
            IREntity::forEachComponent<IRComponents::C_WidgetHotkey>(
                [this](IREntity::EntityId &id, IRComponents::C_WidgetHotkey &hotkey) {
                    if (hotkeyWidgetId_ != IREntity::kNullEntity && id > hotkeyWidgetId_) {
                        return;
                    }
                    const auto &widget = IREntity::getComponent<IRComponents::C_Widget>(id);
                    if (!widget.disabled_ && IRPrefab::Widget::isKeyboardHotkey(hotkey.key_) &&
                        !IRPrefab::Widget::isFocusKey(hotkey.key_) &&
                        hotkey.modifiers_ == heldModifiers_ && pressed(hotkey.key_)) {
                        hotkeyWidgetId_ = id;
                    }
                }
            );
            if (hotkeyWidgetId_ != IREntity::kNullEntity) {
                const auto &widget =
                    IREntity::getComponent<IRComponents::C_Widget>(hotkeyWidgetId_);
                if (IRPrefab::Widget::takesKeyboardFocus(widget)) {
                    IRPrefab::Widget::focus(hotkeyWidgetId_);
                    hotkeyWidgetId_ = IREntity::kNullEntity;
                } else {
                    focusedWidgetId_ = hotkeyWidgetId_;
                    focusVisible_ = true;
                    hotkeyActivates_ = true;
                    if (widget.kind_ == IRComponents::WidgetKind::DROPDOWN) {
                        const auto dropdown =
                            IREntity::getComponentOptional<IRComponents::C_WidgetDropdown>(
                                hotkeyWidgetId_
                            );
                        hotkeyActivates_ = !dropdown || !(*dropdown)->isOpen_;
                    }
                }
            }
        }

        hotkeyCallbacks_.clear();
        for (auto &[key, entry] : IRPrefab::Widget::getHotkeyRegistry()) {
            const IRInput::KeyModifierMask mods = static_cast<IRInput::KeyModifierMask>(key >> 16);
            const IRInput::KeyMouseButtons button =
                static_cast<IRInput::KeyMouseButtons>(key & 0xFFFFu);
            if (!hasFocusedKeyboardWidget() && !IRPrefab::Widget::isFocusKey(button) &&
                pressed(button) && mods == heldModifiers_) {
                hotkeyCallbacks_.push_back(entry.callback_);
            }
        }
        for (auto &callback : hotkeyCallbacks_) {
            callback();
        }
        publishFrame();
    }

    void tick(
        IREntity::EntityId entityId,
        const IRComponents::C_Widget &widget,
        IRComponents::C_WidgetState &state,
        const IRComponents::C_HitBox2DGui &hitbox,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        state.fireAction_ = false;
        state.sliderPressStarted_ = false;
        state.sliderReleased_ = false;
        state.sliderDoubleClick_ = false;
        state.keyAction_ = IRComponents::C_WidgetState::KeyAction::NONE;
        state.keyShiftHeld_ = shiftHeld_;
        state.focused_ = (entityId == focusedWidgetId_);

        if (state.sliderFirstClickRegion_ != IRComponents::C_WidgetState::SliderPressRegion::NONE) {
            ++state.sliderClickAge_;
            if (state.sliderClickAge_ > 18) {
                state.sliderClickAge_ = 0;
                state.sliderFirstClickRegion_ =
                    IRComponents::C_WidgetState::SliderPressRegion::NONE;
            }
        }

        if (widget.disabled_) {
            state.hovered_ = false;
            state.pressed_ = false;
            clearSliderPress(state);
            state.sliderFirstClickRegion_ = IRComponents::C_WidgetState::SliderPressRegion::NONE;
            state.sliderClickAge_ = 0;
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
                if (widget.kind_ == IRComponents::WidgetKind::SLIDER) {
                    state.sliderReleased_ = true;
                    clearSliderPress(state);
                }
                if (effectivelyHovered)
                    state.fireAction_ = true;
            } else if (!mouseLeftDown_) {
                state.pressed_ = false;
                clearSliderPress(state);
            }
        } else if (effectivelyHovered && mouseLeftPressedThisFrame_) {
            state.pressed_ = true;
            capturedWidgetId_ = entityId;
            focusedWidgetId_ = entityId;
            focusVisible_ = false;
            if (widget.kind_ == IRComponents::WidgetKind::SLIDER && widget.size_.x > 0) {
                const float dx = mouseGuiTrixel_.x - static_cast<float>(guiPos.pos_.x);
                state.dragValue_ =
                    IRMath::clamp(dx / static_cast<float>(widget.size_.x), 0.0f, 1.0f);
                state.sliderDoubleClick_ =
                    state.sliderFirstClickRegion_ !=
                        IRComponents::C_WidgetState::SliderPressRegion::NONE &&
                    state.sliderClickAge_ <= 18;
                if (!state.sliderDoubleClick_) {
                    state.sliderPressStarted_ = true;
                    state.sliderPressPosition_ = mouseGuiTrixel_;
                    state.sliderPressFraction_ = state.dragValue_;
                    state.sliderShiftDrag_ = shiftHeld_;
                    state.sliderSteppedDrag_ = controlHeld_;
                }
            }
        }

        if (entityId == hotkeyWidgetId_ && hotkeyActivates_) {
            switch (widget.kind_) {
            case IRComponents::WidgetKind::CHECKBOX:
            case IRComponents::WidgetKind::RADIO:
            case IRComponents::WidgetKind::BUTTON:
            case IRComponents::WidgetKind::COLOR_SWATCH:
            case IRComponents::WidgetKind::HIT_REGION:
            case IRComponents::WidgetKind::DROPDOWN:
                state.fireAction_ = true;
                break;
            default:
                break;
            }
        }
        if (state.focused_ && focusVisible_) {
            using Action = IRComponents::C_WidgetState::KeyAction;
            if (widget.kind_ == IRComponents::WidgetKind::SLIDER) {
                if (pressed(IRInput::kKeyButtonUp))
                    state.keyAction_ = Action::MOVE_UP;
                else if (pressed(IRInput::kKeyButtonDown))
                    state.keyAction_ = Action::MOVE_DOWN;
                else if (pressed(IRInput::kKeyButtonHome))
                    state.keyAction_ = Action::MINIMUM;
                else if (pressed(IRInput::kKeyButtonEnd))
                    state.keyAction_ = Action::MAXIMUM;
                else if (
                    pressed(IRInput::kKeyButtonDelete) || pressed(IRInput::kKeyButtonBackspace)
                )
                    state.keyAction_ = Action::RESET;
            } else if (
                (widget.kind_ == IRComponents::WidgetKind::CHECKBOX ||
                 widget.kind_ == IRComponents::WidgetKind::RADIO) &&
                pressed(IRInput::kKeyButtonSpace)
            ) {
                state.fireAction_ = true;
            } else if (
                (widget.kind_ == IRComponents::WidgetKind::BUTTON ||
                 widget.kind_ == IRComponents::WidgetKind::COLOR_SWATCH ||
                 widget.kind_ == IRComponents::WidgetKind::HIT_REGION) &&
                pressed(IRInput::kKeyButtonEnter)
            ) {
                state.fireAction_ = true;
            } else if (widget.kind_ == IRComponents::WidgetKind::DROPDOWN) {
                if (pressed(IRInput::kKeyButtonSpace))
                    state.keyAction_ = Action::OPEN_OR_CLOSE;
                else if (pressed(IRInput::kKeyButtonUp))
                    state.keyAction_ = Action::MOVE_UP;
                else if (pressed(IRInput::kKeyButtonDown))
                    state.keyAction_ = Action::MOVE_DOWN;
                else if (pressed(IRInput::kKeyButtonEnter))
                    state.keyAction_ = Action::SELECT;
            }
        }
    }

    // Publish the z-ordered topmost hovered widget (resolved in beginTick)
    // into the C_GuiHoverState singleton, if a creation registered one. A
    // once-per-frame singleton write (endTick is allowed to do bounded
    // lookups, unlike the per-entity tick). No singleton → no-op, so
    // non-test creations pay nothing. Read back via
    // IRPrefab::Widget::hoveredWidget(); used by the headless GUI-test
    // harness to assert hover without re-scanning hitboxes.
    void endTick() {
        auto &commands = IRCommand::getCommandManager();
        commands.setKeyboardCaptured(keyboardWidgetFocusedAtStart_ || hasFocusedKeyboardWidget());
        std::span<const IRInput::KeyMouseButtons> captured;
        if ((focusVisibleAtStart_ || focusVisible_) && focusedWidgetId_ != IREntity::kNullEntity &&
            IREntity::entityExists(focusedWidgetId_)) {
            const auto &widget = IREntity::getComponent<IRComponents::C_Widget>(focusedWidgetId_);
            switch (widget.kind_) {
            case IRComponents::WidgetKind::SLIDER:
                captured = kSliderCaptured;
                break;
            case IRComponents::WidgetKind::CHECKBOX:
            case IRComponents::WidgetKind::RADIO:
                captured = kToggleCaptured;
                break;
            case IRComponents::WidgetKind::BUTTON:
            case IRComponents::WidgetKind::COLOR_SWATCH:
            case IRComponents::WidgetKind::HIT_REGION:
                captured = kPressCaptured;
                break;
            case IRComponents::WidgetKind::DROPDOWN: {
                const auto dropdown =
                    IREntity::getComponentOptional<IRComponents::C_WidgetDropdown>(
                        focusedWidgetId_
                    );
                if (dropdown && (*dropdown)->isOpen_) {
                    captured = kOpenDropdownCaptured;
                } else {
                    captured = kClosedDropdownCaptured;
                }
                break;
            }
            default:
                break;
            }
        }
        commands.setCapturedButtons(captured);
        publishFrame();
        IREntity::forEachComponent<IRComponents::C_GuiHoverState>(
            [this](IREntity::EntityId &, IRComponents::C_GuiHoverState &hoverState) {
                hoverState.hoveredWidget_ = topHoveredId_;
            }
        );
    }

    static SystemId create() {
        IRPrefab::Widget::ensureFocusRequestSingleton();
        IRPrefab::Widget::ensureThemeSingleton();
        IREntity::singleton<IRComponents::C_WidgetInputFrame>();
        IREntity::singleton<IRComponents::C_WidgetHotkeyRegistry>();
        const SystemId id = registerSystem<
            WIDGET_INPUT,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_HitBox2DGui,
            IRComponents::C_GuiPosition>("WidgetInput");
        auto *params = getSystemParams<System<WIDGET_INPUT>>(id);
        IREntity::getEntityManager().registerPreDestroyHook([params](IREntity::EntityId destroyed) {
            if (params->capturedWidgetId_ == destroyed) {
                params->capturedWidgetId_ = IREntity::kNullEntity;
            }
            if (params->focusedWidgetId_ == destroyed) {
                params->focusedWidgetId_ = IREntity::kNullEntity;
                params->focusVisible_ = false;
                auto *frame = IREntity::singletonOrNull<IRComponents::C_WidgetInputFrame>();
                if (frame != nullptr) {
                    frame->focusedWidget_ = IREntity::kNullEntity;
                    frame->focusVisible_ = false;
                }
            }
            if (params->topHoveredId_ == destroyed) {
                params->topHoveredId_ = IREntity::kNullEntity;
            }
            params->focusableWidgets_.clear();
            auto *request = IREntity::singletonOrNull<IRComponents::C_WidgetFocusRequest>();
            if (request != nullptr && request->widget_ == destroyed) {
                request->widget_ = IREntity::kNullEntity;
            }
        });
        return id;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_INPUT_H */
