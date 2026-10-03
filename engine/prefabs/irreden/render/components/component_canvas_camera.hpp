#ifndef COMPONENT_CANVAS_CAMERA_H
#define COMPONENT_CANVAS_CAMERA_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

// The camera a voxel-pool canvas is viewed through when it is not the world
// camera. A canvas carrying it rasters at this zoom's subdivision density,
// shades with this view-to-world rotation, and is a window onto its pool:
// content past the canvas edge clips instead of the raster density shrinking
// to fit. A canvas without it is viewed through the world camera.
struct C_CanvasCamera {
    // View-to-world rotation, vec4(qx, qy, qz, qw).
    IRMath::vec4 rotation_{0.0f, 0.0f, 0.0f, 1.0f};
    IRMath::vec2 zoom_{1.0f};
    // Model-space raster offset in base trixels. Both components are integers
    // with an even sum: an odd sum flips the canvas's triangle-lattice parity
    // against the origin the composite reconstructs faces from.
    IRMath::vec2 panIso_{0.0f};
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_CAMERA_H */
