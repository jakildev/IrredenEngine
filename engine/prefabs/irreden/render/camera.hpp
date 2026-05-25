#ifndef IR_PREFAB_CAMERA_H
#define IR_PREFAB_CAMERA_H

// Driver-side API for the camera entity's rotation. All operations apply
// to the engine's named "camera" entity and silently no-op when it has no
// C_LocalTransform yet — this lets scripts and init code run before the
// camera is fully wired without crashing.
//
// Rotation lives in C_LocalTransform.rotation_ (the same SQT quaternion
// every entity uses). The yaw helpers extract the Z-component for the
// cardinal/residual split consumed by the integer trixel rasterizer.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_local_transform.hpp>

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
    const float rasterYaw = std::round(visualYaw / IRMath::kHalfPi) * IRMath::kHalfPi;
    return {rasterYaw, visualYaw - rasterYaw};
}

namespace detail {

inline IRComponents::C_LocalTransform *cameraTransform() {
    const IREntity::EntityId camera = IREntity::getEntity("camera");
    if (camera == IREntity::kNullEntity)
        return nullptr;
    auto opt = IREntity::getComponentOptional<IRComponents::C_LocalTransform>(camera);
    if (!opt.has_value())
        return nullptr;
    return *opt;
}

// Extract Z-yaw from a unit quaternion and wrap to [-π, π).
inline float yawFromQuat(const IRMath::vec4 &q) {
    float yaw = 2.0f * IRMath::atan2(q.z, q.w);
    // Wrap to [-π, π)
    yaw = std::fmod(yaw + IRMath::kPi, IRMath::kTwoPi);
    if (yaw < 0.0f)
        yaw += IRMath::kTwoPi;
    return yaw - IRMath::kPi;
}

// Wrap a yaw value to [-π, π).
inline float wrapYaw(float yaw) {
    float wrapped = std::fmod(yaw + IRMath::kPi, IRMath::kTwoPi);
    if (wrapped < 0.0f)
        wrapped += IRMath::kTwoPi;
    return wrapped - IRMath::kPi;
}

} // namespace detail

/// Set the camera's full rotation as a unit quaternion.
inline void setRotationQuat(const IRMath::vec4 &q) {
    if (auto *t = detail::cameraTransform())
        t->rotation_ = q;
}

/// Camera world rotation as a unit quaternion read from C_LocalTransform.
/// Returns identity (0,0,0,1) when the camera entity is not yet wired.
inline IRMath::vec4 getRotationQuat() {
    if (auto *t = detail::cameraTransform())
        return t->rotation_;
    return IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
}

/// Set the camera's continuous Z-yaw to @p yaw radians (normalized to [-π, π)).
/// Backward-compat shim: writes quatAxisAngle(z, yaw) into C_LocalTransform.
inline void setYaw(float yaw) {
    float wrapped = detail::wrapYaw(yaw);
    setRotationQuat(IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), wrapped));
}

/// Read the camera's Z-yaw (radians, in [-π, π)) extracted from the rotation
/// quaternion. Returns 0 if the camera entity is not yet wired.
inline float getYaw() {
    if (auto *t = detail::cameraTransform())
        return detail::yawFromQuat(t->rotation_);
    return 0.0f;
}

/// Add @p delta (radians) to the camera's yaw, normalized to [-π, π).
inline void rotateYaw(float delta) {
    setYaw(getYaw() + delta);
}

/// Both halves of the yaw split in one call. Prefer this over calling
/// `getRasterYaw()` and `getResidualYaw()` separately when a caller needs
/// both — it does the camera lookup and the split math once.
inline std::pair<float, float> getYawSplit() {
    return computeYawSplit(getYaw());
}

/// Cardinal-snap component of yaw — multiple of π/2 nearest visualYaw.
/// Consumed by the integer trixel raster shader to pick a basis permutation.
/// Still pays for the full split math; use `getYawSplit()` when both halves
/// are needed.
inline float getRasterYaw() {
    return getYawSplit().first;
}

/// Sub-cardinal residual in [-π/4, π/4]; consumed by the screen-space
/// residual composite pass to apply the leftover continuous rotation.
/// Still pays for the full split math; use `getYawSplit()` when both halves
/// are needed.
inline float getResidualYaw() {
    return getYawSplit().second;
}

} // namespace IRPrefab::Camera

#endif /* IR_PREFAB_CAMERA_H */
