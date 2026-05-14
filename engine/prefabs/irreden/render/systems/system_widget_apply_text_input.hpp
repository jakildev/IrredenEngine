#ifndef SYSTEM_WIDGET_APPLY_TEXT_INPUT_H
#define SYSTEM_WIDGET_APPLY_TEXT_INPUT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>

#include <string>
#include <vector>

namespace IRSystem {

// Per-kind follower for text inputs. Polls the GLFW-driven key state
// machine via IRInput. Edits only the focused widget — focus is owned
// by WIDGET_INPUT (Tab cycle + click-to-focus) so the apply tick is
// just "consume the pressed key set this frame and mutate text_ +
// cursorPos_ accordingly".
//
// MVP keymap (T-177): printable A-Z, 0-9, space, period, comma, slash,
// hyphen-minus and the shifted-letter case (Shift→uppercase). Editing
// keys: Backspace, Delete, Left, Right, Home, End. Multi-char IME and
// the shifted-digit symbol map are deliberately out of scope for the
// first cut; the framework can be extended without changing this
// system's archetype filter.
template <> struct System<WIDGET_APPLY_TEXT_INPUT> {
    struct PendingKey {
        char glyph_ = 0;       // 0 = control key
        enum class Control : int { NONE, BACKSPACE, DELETE, LEFT, RIGHT, HOME, END } control_ =
            Control::NONE;
    };
    std::vector<PendingKey> pending_;
    bool reserved_ = false;

    void beginTick() {
        if (!reserved_) {
            pending_.reserve(16);
            reserved_ = true;
        }
        pending_.clear();

        const bool shift = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift);

        // Letters
        for (int i = 0; i < 26; ++i) {
            const auto key =
                static_cast<IRInput::KeyMouseButtons>(IRInput::kKeyButtonA + i);
            if (IRInput::checkKeyMouseButton(key, IRInput::ButtonStatuses::PRESSED)) {
                pending_.push_back({static_cast<char>((shift ? 'A' : 'a') + i),
                                    PendingKey::Control::NONE});
            }
        }
        // Digits — no shifted-symbol map in MVP; ignore shift.
        for (int i = 0; i < 10; ++i) {
            const auto key =
                static_cast<IRInput::KeyMouseButtons>(IRInput::kKeyButton0 + i);
            if (IRInput::checkKeyMouseButton(key, IRInput::ButtonStatuses::PRESSED)) {
                pending_.push_back({static_cast<char>('0' + i), PendingKey::Control::NONE});
            }
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonSpace,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({' ', PendingKey::Control::NONE});
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonPeriod,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({'.', PendingKey::Control::NONE});
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonComma,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({',', PendingKey::Control::NONE});
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonMinus,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({'-', PendingKey::Control::NONE});
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonSlash,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({'/', PendingKey::Control::NONE});
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonBackspace,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({0, PendingKey::Control::BACKSPACE});
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonDelete,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({0, PendingKey::Control::DELETE});
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonLeft,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({0, PendingKey::Control::LEFT});
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonRight,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({0, PendingKey::Control::RIGHT});
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonHome,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({0, PendingKey::Control::HOME});
        }
        if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonEnd,
                                         IRInput::ButtonStatuses::PRESSED)) {
            pending_.push_back({0, PendingKey::Control::END});
        }
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetTextInput &ti
    ) {
        if (widget.disabled_) return;
        if (!state.focused_) return;
        if (pending_.empty()) return;

        for (const auto &key : pending_) {
            if (key.control_ == PendingKey::Control::BACKSPACE) {
                if (ti.cursorPos_ > 0 && !ti.text_.empty()) {
                    ti.text_.erase(static_cast<std::size_t>(ti.cursorPos_ - 1), 1);
                    --ti.cursorPos_;
                }
            } else if (key.control_ == PendingKey::Control::DELETE) {
                if (ti.cursorPos_ < static_cast<int>(ti.text_.size())) {
                    ti.text_.erase(static_cast<std::size_t>(ti.cursorPos_), 1);
                }
            } else if (key.control_ == PendingKey::Control::LEFT) {
                ti.cursorPos_ = IRMath::max(0, ti.cursorPos_ - 1);
            } else if (key.control_ == PendingKey::Control::RIGHT) {
                ti.cursorPos_ = IRMath::min(static_cast<int>(ti.text_.size()), ti.cursorPos_ + 1);
            } else if (key.control_ == PendingKey::Control::HOME) {
                ti.cursorPos_ = 0;
            } else if (key.control_ == PendingKey::Control::END) {
                ti.cursorPos_ = static_cast<int>(ti.text_.size());
            } else if (key.glyph_ != 0) {
                if (ti.maxLength_ > 0 &&
                    static_cast<int>(ti.text_.size()) >= ti.maxLength_) {
                    continue;
                }
                ti.text_.insert(static_cast<std::size_t>(ti.cursorPos_), 1, key.glyph_);
                ++ti.cursorPos_;
            }
        }

        const int n = static_cast<int>(ti.text_.size());
        ti.cursorPos_ = IRMath::clamp(ti.cursorPos_, 0, n);
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_APPLY_TEXT_INPUT,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetTextInput
        >("WidgetApplyTextInput");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_TEXT_INPUT_H */
