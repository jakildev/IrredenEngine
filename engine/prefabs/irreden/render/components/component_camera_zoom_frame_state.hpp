#ifndef COMPONENT_CAMERA_ZOOM_FRAME_STATE_H
#define COMPONENT_CAMERA_ZOOM_FRAME_STATE_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

/// Runtime state of a continuous-zoom camera: the placement sample the
/// composite published for the frame, which is also the prior sample the next
/// frame's phase advances from. Derived every frame from the camera and the
/// zoom; never authored, saved, or carried across a policy change.
///
/// Written only by `IRPrefab::Camera::prepareZoomFrame` and the policy
/// setter's reset. Every stage that places camera-following content reads it
/// through `IRPrefab::Camera::zoomFrame`.
struct C_CameraZoomFrameState {
    IRMath::CameraRasterPhase sample_{};
    /// False until a frame has been prepared since the last reset. A reader
    /// must not place content from `sample_` while this is false.
    bool published_ = false;

    C_CameraZoomFrameState() {}
};

} // namespace IRComponents

#endif /* COMPONENT_CAMERA_ZOOM_FRAME_STATE_H */
