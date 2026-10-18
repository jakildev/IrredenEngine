#ifndef SYSTEM_WIDGET_APPLY_NUMBER_FIELD_H
#define SYSTEM_WIDGET_APPLY_NUMBER_FIELD_H

#include <irreden/ir_input.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_widget.hpp>

#include <string>

namespace IRSystem {

template <> struct System<WIDGET_APPLY_NUMBER_FIELD> {
    std::string typed_;
    bool backspace_ = false;
    bool escape_ = false;

    void beginTick() {
        typed_.reserve(3);
        typed_.clear();
        for (int index = 0; index < 10; ++index) {
            const auto key = static_cast<IRInput::KeyMouseButtons>(IRInput::kKeyButton0 + index);
            if (IRInput::checkKeyMouseButton(key, IRInput::ButtonStatuses::PRESSED)) {
                typed_.push_back(static_cast<char>('0' + index));
            }
        }
        if (IRInput::checkKeyMouseButton(
                IRInput::kKeyButtonMinus,
                IRInput::ButtonStatuses::PRESSED
            )) {
            typed_.push_back('-');
        }
        if (IRInput::checkKeyMouseButton(
                IRInput::kKeyButtonPeriod,
                IRInput::ButtonStatuses::PRESSED
            )) {
            typed_.push_back('.');
        }
        backspace_ = IRInput::checkKeyMouseButton(
            IRInput::kKeyButtonBackspace,
            IRInput::ButtonStatuses::PRESSED
        );
        escape_ = IRInput::checkKeyMouseButton(
            IRInput::kKeyButtonEscape,
            IRInput::ButtonStatuses::PRESSED
        );
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetNumberField &field
    ) {
        const int glyphWidth = IRRender::glyphMetrics(field.textSize_).stepX_;
        const int innerWidth = IRMath::max(0, widget.size_.x - 4);
        const int maxCharacters = IRMath::clamp(innerWidth / glyphWidth, 0, 16);
        field.updateEdit(
            state.focused_,
            widget.disabled_,
            typed_,
            backspace_,
            escape_,
            maxCharacters
        );
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_APPLY_NUMBER_FIELD,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetNumberField>("WidgetApplyNumberField");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_NUMBER_FIELD_H */
