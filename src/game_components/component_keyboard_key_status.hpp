/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_keyboard_key_status.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_KEYBOARD_KEY_STATUS_H
#define COMPONENT_KEYBOARD_KEY_STATUS_H

#include "../math/ir_math.hpp"
#include "../input/ir_input.hpp"
#include "component_tags_all.hpp"

using namespace IRMath;
using namespace IRInput;

namespace IRComponents {

    struct C_KeyStatus {
        IRButtonStatuses status_;
        // A key can be pressed and released in the same frame
        int pressedThisFrameCount_;
        int releasedThisFrameCount_;

        C_KeyStatus(IRButtonStatuses status)
        :   status_{status}
        ,   pressedThisFrameCount_{0}
        ,   releasedThisFrameCount_{0}
        {

        }

        // Default
        C_KeyStatus()
        :   status_(IRButtonStatuses::kNotHeld)
        ,   pressedThisFrameCount_{0}
        ,   releasedThisFrameCount_{0}

        {

        }

    };

    struct C_KeyPressed{};
    struct C_KeyReleased{};

} // namespace IRComponents




#endif /* COMPONENT_KEYBOARD_KEY_STATUS_H */
