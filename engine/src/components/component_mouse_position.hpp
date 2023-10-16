/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_mouse_position.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_MOUSE_POSITION_H
#define COMPONENT_MOUSE_POSITION_H

#include "../math/ir_math.hpp"
#include "../world/ir_constants.hpp"
#include "component_tags_all.hpp"

using namespace IRMath;

namespace IRComponents {

    struct C_MousePosition {
        dvec2 pos_;

        // default
        C_MousePosition()
        :   pos_{0.0}
        {

        }

    };

} // namespace IRComponents

#endif /* COMPONENT_MOUSE_POSITION_H */
