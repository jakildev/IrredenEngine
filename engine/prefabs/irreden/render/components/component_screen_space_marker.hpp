#ifndef COMPONENT_SCREEN_SPACE_MARKER_H
#define COMPONENT_SCREEN_SPACE_MARKER_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

// Runtime overlay state in top-left-origin logical window points. DEBUG_OVERLAY
// composites the cross after framebuffer output, independent of world depth/yaw.
struct C_ScreenSpaceMarker {
    IRMath::vec2 windowPoint_{0.0f};
    float radiusPoints_ = 6.0f;
    IRMath::Color color_{255, 255, 255, 255};
    bool visible_ = false;
};

} // namespace IRComponents

#endif /* COMPONENT_SCREEN_SPACE_MARKER_H */
