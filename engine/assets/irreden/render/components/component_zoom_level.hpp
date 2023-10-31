/*
 * Project: Irreden Engine
 * File: component_zoom_level.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_ZOOM_LEVEL_H
#define COMPONENT_ZOOM_LEVEL_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRComponents {

    struct C_ZoomLevel {
        vec2 zoom_;

        C_ZoomLevel(vec2 zoom)
        :   zoom_{zoom}
        {

        }

        C_ZoomLevel(float zoom)
        :   C_ZoomLevel{vec2(zoom, zoom)}
        {

        }

        // Default
        C_ZoomLevel()
        :   C_ZoomLevel{vec2(0, 0)}
        {

        }

    };

} // namespace IRComponents


#endif /* COMPONENT_ZOOM_LEVEL_H */
