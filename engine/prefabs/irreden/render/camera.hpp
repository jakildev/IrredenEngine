#ifndef IR_PREFAB_CAMERA_H
#define IR_PREFAB_CAMERA_H

// Driver-side API for the camera entity's Z-yaw rotation. All operations
// apply to the engine's named "camera" entity and silently no-op when it
// has no `C_CameraYaw` component yet — this lets scripts and init code
// run before the camera is fully wired without crashing.

#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_camera_yaw.hpp>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <utility>

namespace IRPrefab::Camera {

/// Decompose a continuous Z-yaw value into (rasterYaw, residualYaw):
///   rasterYaw   = nearest cardinal multiple of π/2 to @p visualYaw
///   residualYaw = visualYaw - rasterYaw, lies in [-π/4, π/4]
/// rasterYaw selects the cardinal-snap basis for the integer trixel
/// rasterizer; residualYaw is the leftover angle the screen-space
/// residual composite pass rotates the canvas by.
inline std::pair<float, float> computeYawSplit(float visualYaw) {
    constexpr float halfPi = glm::half_pi<float>();
    const float rasterYaw = std::round(visualYaw / halfPi) * halfPi;
    return {rasterYaw, visualYaw - rasterYaw};
}

namespace detail {

inline IRComponents::C_CameraYaw *cameraYawComponent() {
    const IREntity::EntityId camera = IREntity::getEntity("camera");
    if (camera == IREntity::kNullEntity) return nullptr;
    auto opt = IREntity::getComponentOptional<IRComponents::C_CameraYaw>(camera);
    if (!opt.has_value()) return nullptr;
    return *opt;
}

} // namespace detail

/// Set the camera's continuous Z-yaw to @p yaw radians (normalized to [-π, π)).
inline void setYaw(float yaw) {
    if (auto *c = detail::cameraYawComponent()) c->setVisualYaw(yaw);
}

/// Read the camera's continuous Z-yaw (radians, in [-π, π)).
/// Returns 0 if the camera entity has no yaw component yet.
inline float getYaw() {
    if (auto *c = detail::cameraYawComponent()) return c->visualYaw_;
    return 0.0f;
}

/// Add @p delta (radians) to the camera's yaw, normalized to [-π, π).
inline void rotateYaw(float delta) {
    if (auto *c = detail::cameraYawComponent()) c->rotate(delta);
}

/// Both halves of the yaw split in one call. Prefer this over calling
/// `getRasterYaw()` and `getResidualYaw()` separately when a caller needs
/// both — it does the camera lookup and the split math once.
inline std::pair<float, float> getYawSplit() { return computeYawSplit(getYaw()); }

/// Cardinal-snap component of yaw — multiple of π/2 nearest visualYaw.
/// Consumed by the integer trixel raster shader to pick a basis permutation.
inline float getRasterYaw() { return getYawSplit().first; }

/// Sub-cardinal residual in [-π/4, π/4]; consumed by the screen-space
/// residual composite pass to apply the leftover continuous rotation.
inline float getResidualYaw() { return getYawSplit().second; }

} // namespace IRPrefab::Camera

#endif /* IR_PREFAB_CAMERA_H */
