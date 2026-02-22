#ifndef COMPONENT_KEYBOARD_KEY_H
#define COMPONENT_KEYBOARD_KEY_H

namespace IRComponents {

struct C_KeyboardKey {
    int key_;

    C_KeyboardKey(int key)
        : key_(key) {}

    // Default
    C_KeyboardKey()
        : key_(-1) {}
};

} // namespace IRComponents

#endif /* COMPONENT_KEYBOARD_KEY_H */
