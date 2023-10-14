/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_hitbox_circle.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_HITBOX_CIRCLE_H
#define COMPONENT_HITBOX_CIRCLE_H

#include "../math/ir_math.hpp"
#include "component_tags_all.hpp"

namespace IRComponents {

    struct C_HitboxCircle {
        int radius_;

        C_HitboxCircle(int radius)
        :   radius_(radius)
        {

        }

        // Default
        C_HitboxCircle()
        :   radius_(0)
        {

        }

    };

} // namespace IRComponents


#endif /* COMPONENT_HITBOX_CIRCLE_H */
