/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_keyboard_key.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_KEYBOARD_KEY_H
#define COMPONENT_KEYBOARD_KEY_H

#include "../math/ir_math.hpp"
#include "component_tags_all.hpp"

using namespace IRMath;

namespace IRComponents {

    struct C_KeyboardKey {
        int key_;

        C_KeyboardKey(int key)
        :   key_(key)
        {

        }

        // Default
        C_KeyboardKey()
        :   key_(-1)
        {

        }

    };

} // namespace IRComponents

#endif /* COMPONENT_KEYBOARD_KEY_H */