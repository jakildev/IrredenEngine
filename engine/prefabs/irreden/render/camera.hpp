#ifndef IR_PREFAB_CAMERA_H
#define IR_PREFAB_CAMERA_H

// Driver-side API for the camera entity's rotation. All operations
// apply to the engine's named "camera" entity and silently no-op when
// it has no `C_LocalTransform` yet — this lets scripts and init code
// run before the camera is fully wired without crashing.
//
// Rotation lives on the camera entity's `C_LocalTransform.rotation_`
// (auto-attached by `createEntity` like every other entity, per the
// T-295 "single source of truth" disposition). GRID consumers
// (picking / hitbox / gizmo / SDF cull / integer raster) only ever
// see the Z-component via the helpers below; DETACHED consumers see
// the full SO(3) quaternion through `getRotationQuat()`.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_local_transform.hpp>

#include <utility>

namespace IRPrefab::Camera {

/// Decompose a continuous Z-yaw value into (rasterYaw, residualYaw):
///   rasterYaw   = nearest cardinal multiple of π/2 to @p visualYaw
///   residualYaw = visualYaw - rasterYaw, lies in [-π/4, π/4]
/// rasterYaw selects the cardinal-snap basis for the integer trixel
/// rasterizer; residualYaw is the leftover angle the screen-space
/// residual composite pass rotates the canvas by.
inline std::pair<float, float> computeYawSplit(float visualYaw) {
    const float rasterYaw =
        static_cast<float>(IRMath::round(visualYaw / IRMath::kHalfPi)) * IRMath::kHalfPi;
    return {rasterYaw, visualYaw - rasterYaw};
}

namespace detail {

inline IRComponents::C_LocalTransform *cameraLocalTransform() {
    const IREntity::EntityId camera = IREntity::getEntity("camera");
    if (camera == IREntity::kNullEntity)
        return nullptr;
    auto opt = IREntity::getComponentOptional<IRComponents::C_LocalTransform>(camera);
    if (!opt.has_value())
        return nullptr;
    return *opt;
}

} // namespace detail

/// Camera world rotation as a unit quaternion. Returns identity
/// `(0,0,0,1)` when the camera entity has no transform yet, preserving
/// the no-op contract of `getYaw()`. The full SO(3) quaternion is used
/// directly by DETACHED entities (composed in
/// `PROPAGATE_CANVAS_ROTATION` via `IRMath::quatInverse` to cancel the
/// camera basis on the per-entity canvas bake); GRID consumers only
/// see its Z-component through the yaw helpers below.
inline IRMath::vec4 getRotationQuat() {
    if (auto *c = detail::cameraLocalTransform())
        return c->rotation_;
    return IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
}

/// Set the camera's full SO(3) rotation. `q` must be a unit quaternion
/// — the layout matches `IRMath::quatAxisAngle` output (xyz, w).
inline void setRotationQuat(const IRMath::vec4 &q) {
    if (auto *c = detail::cameraLocalTransform())
        c->rotation_ = q;
}

/// Read the camera's continuous Z-yaw (radians, in (-π, π]).
/// Returns 0 if the camera entity has no transform yet. Extracts the
/// Z-axis Tait-Bryan angle from `C_LocalTransform.rotation_`, which
/// round-trips exactly for pure-Z rotations written via `setYaw`.
inline float getYaw() {
    return IRMath::quatExtractZAngle(getRotationQuat());
}

/// Set the camera's continuous Z-yaw to @p yaw radians. Backward-compat
/// shim that writes `quatAxisAngle(z, yaw)` to `C_LocalTransform.rotation_`
/// — overrides any prior pitch/roll. Callers wanting to preserve
/// pitch/roll should use `setRotationQuat` directly.
inline void setYaw(float yaw) {
    setRotationQuat(IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), yaw));
}

/// Add @p delta (radians) to the camera's Z-yaw. Round-trips through
/// `getYaw` / `setYaw` so pure-Z behavior matches the pre-SO(3) API
/// exactly; any prior pitch/roll on the camera is discarded.
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
