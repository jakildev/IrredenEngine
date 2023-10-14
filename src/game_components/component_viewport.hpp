/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_viewport_state.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_VIEWPORT_H
#define COMPONENT_VIEWPORT_H

#include "../math/ir_math.hpp"
#include "component_tags_all.hpp"

using namespace IRMath;

namespace IRComponents {

    struct C_ViewportXY {

        int x_;
        int y_;

        C_ViewportXY(int x, int y)
        :   x_{x}
        ,   y_{y}
        {

        }

        // Default
        C_ViewportXY()
        :   x_{0}
        ,   y_{0}
        {

        }

    };

} // namespace IRComponents




#endif /* COMPONENT_VIEWPORT_STATE_H */
