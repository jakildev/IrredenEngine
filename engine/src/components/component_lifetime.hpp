/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_lifetime.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_LIFETIME_H
#define COMPONENT_LIFETIME_H

#include "../math/ir_math.hpp"
#include "component_tags_all.hpp"

using namespace IRMath;

namespace IRComponents {

    struct C_Lifetime {
        int life_;

        C_Lifetime(int life)
        :   life_(life)
        {

        }

        // Default
        C_Lifetime()
        :   life_(1)
        {

        }

    };

} // namespace IRComponents


#endif /* COMPONENT_LIFETIME_H */
