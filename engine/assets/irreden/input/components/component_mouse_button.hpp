/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_mouse_button.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_MOUSE_BUTTON_H
#define COMPONENT_MOUSE_BUTTON_H

#include <irreden/ir_math.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;

namespace IRComponents {

    struct C_KeyMouseButton {
        int button_;

        C_KeyMouseButton(int mouseButton)
        :   button_(mouseButton)
        {

        }

        // Default
        C_KeyMouseButton()
        :   button_(-1)
        {

        }

    };

    struct C_MouseScroll {
        double xoffset_;
        double yoffset_;

        C_MouseScroll(
            double xoffset,
            double yoffset
        )
        :   xoffset_(xoffset)
        ,   yoffset_(yoffset)
        {

        }

        // Default
        C_MouseScroll()
        :   xoffset_(0)
        ,   yoffset_(0)
        {

        }
    };

} // namespace IRComponents

#endif /* COMPONENT_MOUSE_BUTTON_H */
