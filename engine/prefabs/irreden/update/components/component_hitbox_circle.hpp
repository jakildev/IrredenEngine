/*
 * Project: Irreden Engine
 * File: component_hitbox_circle.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_HITBOX_CIRCLE_H
#define COMPONENT_HITBOX_CIRCLE_H

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
