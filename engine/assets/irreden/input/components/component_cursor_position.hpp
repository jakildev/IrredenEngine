/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_cursor_position.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_CURSOR_POSITION_H
#define COMPONENT_CURSOR_POSITION_H

// TODO: Should i keep a seperate position for rendering and game logic?
// Probably not, but I do want the cursor position to update with the
// renderer and not the logic, right????

#include <irreden/ir_math.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;

namespace IRComponents {

    struct C_CursorPosition {

        double posX_;
        double posY_;
        vec2 hoveredTriangleIndexScreen_;

        C_CursorPosition(double x, double y)
        :   posX_{x}
        ,   posY_{y}
        ,   hoveredTriangleIndexScreen_{-1, -1}
        {

        }

        // Default
        C_CursorPosition()
        :   posX_{0.0}
        ,   posY_{0.0}
        {

        }

    };

} // namespace IRComponents

#endif /* COMPONENT_CURSOR_POSITION_H */
