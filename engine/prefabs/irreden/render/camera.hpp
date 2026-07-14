#ifndef IR_PREFAB_CAMERA_H
#define IR_PREFAB_CAMERA_H

// Driver-side API for the camera entity's rotation. All operations apply
// to the engine's named "camera" entity and silently no-op when it has no
// C_LocalTransform yet — this lets scripts and init code run before the
// camera is fully wired without crashing.
//
// Rotation lives in C_LocalTransform.rotation_ (the same SQT quaternion
// every entity uses). The primary convention is ZX composition:
// q = qZ(yaw) × qX(pitch). The GRID trixel rasterizer extracts only
// Z-yaw for its cardinal/residual split; DETACHED canvases use the full
// quaternion via system_propagate_canvas_rotation.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_local_transform.hpp>

#include <utility>

namespace IRPrefab::Camera {

// Residual-yaw deadband. A residual at or below this magnitude counts as a
// settled cardinal (not rotating): the renderer stays on the byte-identical
// single-canvas fast path and the per-axis textures stay released. This is the
// ONE place the threshold lives — `computeYawSplit` snaps the residual to
// exactly 0 inside it, so every "rotating?" consumer (the per-axis allocation
// gate in per_axis_canvas.hpp, the render path-select gate in
// system_voxel_to_trixel.hpp, the FrameDataVoxelToCanvas UBO, the sun-shadow
// bake's frame patch) derives the same predicate from the same value and cannot
// disagree. A split predicate freed the per-axis textures while the per-axis
// path still read them, causing see-through coverage holes at 90°/180° (#1882).
inline constexpr float kResidualYawDeadband = 1e-4f;

/// Decompose a continuous Z-yaw value into (rasterYaw, residualYaw):
///   rasterYaw   = nearest cardinal multiple of π/2 to @p visualYaw
///   residualYaw = visualYaw - rasterYaw, lies in [-π/4, π/4], reported as
///                 exactly 0 within kResidualYawDeadband (see above)
/// rasterYaw selects the cardinal-snap basis for the integer trixel
/// rasterizer; residualYaw is the leftover angle the screen-space
/// residual composite pass rotates the canvas by.
inline std::pair<float, float> computeYawSplit(float visualYaw) {
    const float rasterYaw = IRMath::round(visualYaw / IRMath::kHalfPi) * IRMath::kHalfPi;
    const float residualYaw = visualYaw - rasterYaw;
    // Single-source deadband (#1882): a residual within float-noise of a
    // cardinal reports as exactly 0 so the allocation gate and the render
    // path-select gate agree. No-op for visible rotation (|residual| >
    // deadband) and for an exact cardinal (residual already 0) — byte-identical.
    if (IRMath::abs(residualYaw) <= kResidualYawDeadband) {
        return {rasterYaw, 0.0f};
    }
    return {rasterYaw, residualYaw};
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

// Extract Z-yaw from the ZX-composed camera quaternion, wrapped to [-π, π).
// For q = qZ(yaw) × qX(pitch): atan2(q.z, q.w) = yaw/2 when pitch is
// clamped to ±(π/2 − ε) as guaranteed by clampPitch.
inline float yawFromQuat(const IRMath::vec4 &q) {
    return IRMath::wrapAnglePi(2.0f * IRMath::atan2(q.z, q.w));
}

// Extract X-pitch from the ZX-composed camera quaternion.
// For q = qZ(yaw) × qX(pitch): atan2(q.x, q.w) = pitch/2 when
// cos(yaw/2) > 0 (yaw ≠ ±π). The ε guard in wrapYaw keeps the result
// strictly above -π, so cos(yaw/2) > 0 is guaranteed for all callers.
inline float pitchFromQuat(const IRMath::vec4 &q) {
    return 2.0f * IRMath::atan2(q.x, q.w);
}

inline float wrapYaw(float yaw) {
    // Keep strictly above -π so pitchFromQuat's atan2(q.x, q.w) stays valid.
    // The 1e-4 gap is invisible to the rasterizer (<1/10 000 of π/2 ≈ 0°0.006').
    return IRMath::max(IRMath::wrapAnglePi(yaw), -IRMath::kPi + 1e-4f);
}

static constexpr float kPitchLimit = IRMath::kHalfPi - 0.01f;

inline float clampPitch(float pitch) {
    return IRMath::clamp(pitch, -kPitchLimit, kPitchLimit);
}

// Compose a ZX quaternion from yaw (Z) and pitch (X).
inline IRMath::vec4 quatFromYawPitch(float yaw, float pitch) {
    const IRMath::vec4 qZ = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), yaw);
    const IRMath::vec4 qX = IRMath::quatAxisAngle(IRMath::vec3(1.0f, 0.0f, 0.0f), pitch);
    return IRMath::quatMul(qZ, qX);
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

/// Read the camera's Z-yaw (radians, in [-π, π)) extracted from the rotation
/// quaternion. Returns 0 if the camera entity is not yet wired.
inline float getYaw() {
    if (auto *t = detail::cameraTransform())
        return detail::yawFromQuat(t->rotation_);
    return 0.0f;
}

/// Read the camera's X-pitch (radians) from the ZX-composed quaternion.
/// Returns 0 if the camera entity is not yet wired.
inline float getPitch() {
    if (auto *t = detail::cameraTransform())
        return detail::pitchFromQuat(t->rotation_);
    return 0.0f;
}

/// Set both yaw and pitch in one call. Composes as qZ(yaw) × qX(pitch).
/// Pitch is clamped to ±(π/2 - ε) to avoid gimbal lock.
inline void setYawPitch(float yaw, float pitch) {
    setRotationQuat(detail::quatFromYawPitch(detail::wrapYaw(yaw), detail::clampPitch(pitch)));
}

/// Set the camera's Z-yaw, preserving the current pitch.
inline void setYaw(float yaw) {
    setYawPitch(yaw, getPitch());
}

/// Add @p delta (radians) to the camera's yaw, preserving pitch.
inline void rotateYaw(float delta) {
    setYaw(getYaw() + delta);
}

/// Set the camera's X-pitch, preserving the current yaw.
/// Clamped to ±(π/2 - ε) to avoid gimbal lock.
inline void setPitch(float pitch) {
    setYawPitch(getYaw(), pitch);
}

/// Add @p delta (radians) to the camera's pitch, preserving yaw.
inline void rotatePitch(float delta) {
    setPitch(getPitch() + delta);
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
