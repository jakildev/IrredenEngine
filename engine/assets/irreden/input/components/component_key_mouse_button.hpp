#ifndef COMPONENT_KEY_MOUSE_BUTTON_H
#define COMPONENT_KEY_MOUSE_BUTTON_H

#include <irreden/input/ir_input_types.hpp>

namespace IRComponents {

    struct C_KeyMouseButton {
        IRInput::KeyMouseButtons button_;

        C_KeyMouseButton(IRInput::KeyMouseButtons mouseButton)
        :   button_(mouseButton)
        {

        }

        C_KeyMouseButton()
        :   button_(IRInput::KeyMouseButtons::kNullButton)
        {

        }
    };

} // namespace IRComponents

#endif /* COMPONENT_KEY_MOUSE_BUTTON_H */
