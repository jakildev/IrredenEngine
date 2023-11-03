#ifndef COMPONENT_KEY_MOUSE_BUTTON_H
#define COMPONENT_KEY_MOUSE_BUTTON_H

namespace IRComponents {

    struct C_KeyMouseButton {
        int button_;

        C_KeyMouseButton(int mouseButton)
        :   button_(mouseButton)
        {

        }

        C_KeyMouseButton()
        :   button_(-1)
        {

        }

    };

} // namespace IRComponents

#endif /* COMPONENT_KEY_MOUSE_BUTTON_H */
