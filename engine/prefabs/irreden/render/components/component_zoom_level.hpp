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
#include <irreden/ir_constants.hpp>

using IRMath::vec2;

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
        :   C_ZoomLevel{vec2(1, 1)}
        {

        }

        void zoomIn() {
            zoom_ = round(glm::clamp(
                zoom_ * vec2(2.0f),
                IRConstants::kTriangleCanvasZoomMin,
                IRConstants::kTriangleCanvasZoomMax
            ));
        }

        void zoomOut() {
            zoom_ = round(glm::clamp(
                zoom_ / vec2(2.0f),
                IRConstants::kTriangleCanvasZoomMin,
                IRConstants::kTriangleCanvasZoomMax
            ));
        }

    };

} // namespace IRComponents


#endif /* COMPONENT_ZOOM_LEVEL_H */
