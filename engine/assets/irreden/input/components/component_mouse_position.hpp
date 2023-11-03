/*
 * Project: Irreden Engine
 * File: component_mouse_position.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_MOUSE_POSITION_H
#define COMPONENT_MOUSE_POSITION_H

#include <irreden/ir_math.hpp>

using IRMath::dvec2;

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
