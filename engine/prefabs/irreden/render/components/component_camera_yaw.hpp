#ifndef COMPONENT_CAMERA_YAW_H
#define COMPONENT_CAMERA_YAW_H

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace IRComponents {

/// Continuous Z-axis world rotation (radians) applied to the camera view.
///
/// `visualYaw_` is the canonical input gameplay writes; the renderer derives
/// `(rasterYaw, residualYaw)` per frame via `IRPrefab::Camera::computeYawSplit`.
/// Stored value is normalized to [-π, π) on every mutation so floating-point
/// drift across long rotation sequences cannot accumulate.
struct C_CameraYaw {
    float visualYaw_;

    explicit C_CameraYaw(float yaw)
        : visualYaw_{wrap(yaw)} {}

    C_CameraYaw()
        : C_CameraYaw{0.0f} {}

    void setVisualYaw(float yaw) { visualYaw_ = wrap(yaw); }

    void rotate(float delta) { visualYaw_ = wrap(visualYaw_ + delta); }

private:
    static float wrap(float yaw) {
        constexpr float twoPi = glm::two_pi<float>();
        constexpr float pi = glm::pi<float>();
        float wrapped = std::fmod(yaw + pi, twoPi);
        if (wrapped < 0.0f) wrapped += twoPi;
        return wrapped - pi;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CAMERA_YAW_H */
