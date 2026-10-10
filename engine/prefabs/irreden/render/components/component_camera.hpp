#ifndef COMPONENT_CAMERA_H
#define COMPONENT_CAMERA_H

namespace IRComponents {

struct C_Camera {
    /// Zoom policy of the camera this tags. False: every zoom write snaps to a
    /// power of two. True: the zoom is any value in the engine's zoom range and
    /// the composite places content with a carried raster phase. Flip it through
    /// `IRPrefab::Camera::setZoomContinuous`, which also restores the snapped
    /// invariant on the way out.
    bool continuousZoom_ = false;

    // Default
    C_Camera() {}
};

} // namespace IRComponents

#endif /* COMPONENT_CAMERA_H */
