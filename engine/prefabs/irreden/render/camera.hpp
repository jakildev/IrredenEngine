#ifndef IR_PREFAB_CAMERA_H
#define IR_PREFAB_CAMERA_H

// Driver-side API for the camera entity's rotation and zoom policy. All
// operations apply to the engine's named "camera" entity and silently no-op
// when the component they need is not on it yet — this lets scripts and init
// code run before the camera is fully wired without crashing.
//
// Rotation lives in C_LocalTransform.rotation_ (the same SQT quaternion
// every entity uses). The primary convention is ZX composition:
// q = qZ(yaw) × qX(pitch). The GRID trixel rasterizer extracts only
// Z-yaw for its cardinal/residual split; DETACHED canvases use the full
// quaternion via system_propagate_canvas_rotation.
//
// Zoom policy lives in C_Camera.continuousZoom_. The zoom VALUE stays in
// C_ZoomLevel and is written through IRRender::setCameraZoom; this header
// owns what a write means under each policy and the per-frame raster phase
// the continuous policy composites with.

#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_camera_zoom_frame_state.hpp>
#include <irreden/render/components/component_zoom_level.hpp>

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
// path still read them, causing see-through coverage holes at 90°/180°.
inline constexpr float kResidualYawDeadband = 1e-4f;

/// Decompose a continuous Z-yaw value into (rasterYaw, residualYaw):
///   rasterYaw   = nearest cardinal multiple of π/2 to @p visualYaw
///   residualYaw = visualYaw - rasterYaw, lies in [-π/4, π/4], reported as
///                 exactly 0 within kResidualYawDeadband
/// rasterYaw selects the cardinal-snap basis for the integer trixel
/// rasterizer; residualYaw is the leftover angle used by the screen-space
/// composite.
inline std::pair<float, float> computeYawSplit(float visualYaw) {
    const float rasterYaw = IRMath::round(visualYaw / IRMath::kHalfPi) * IRMath::kHalfPi;
    const float residualYaw = visualYaw - rasterYaw;
    // Single-source deadband: a residual within float-noise of a
    // cardinal reports as exactly 0 so the allocation gate and the render
    // path-select gate agree. No-op for visible rotation (|residual| >
    // deadband) and for an exact cardinal (residual already 0) — byte-identical.
    if (IRMath::abs(residualYaw) <= kResidualYawDeadband) {
        return {rasterYaw, 0.0f};
    }
    return {rasterYaw, residualYaw};
}

namespace detail {

template <typename Component> inline Component *cameraComponent() {
    const IREntity::EntityId camera = IREntity::getEntity("camera");
    if (camera == IREntity::kNullEntity)
        return nullptr;
    auto opt = IREntity::getComponentOptional<Component>(camera);
    if (!opt.has_value())
        return nullptr;
    return *opt;
}

inline IRComponents::C_LocalTransform *cameraTransform() {
    return cameraComponent<IRComponents::C_LocalTransform>();
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

/// The zoom a scalar write of @p requested stores: clamped to the engine zoom
/// range, then snapped to the nearest power of two unless @p continuous.
inline float resolveZoomRequest(float requested, bool continuous) {
    const float clamped = IRMath::clamp(
        requested,
        IRConstants::kTrixelCanvasZoomMin.x,
        IRConstants::kTrixelCanvasZoomMax.x
    );
    return continuous ? clamped : IRMath::snapToPowerOfTwo(clamped);
}

/// Each axis of @p zoom snapped to its nearest power of two inside the engine
/// zoom range. Per axis, so an anisotropic zoom keeps its ratio's nearest
/// snapped form instead of collapsing to uniform.
inline IRMath::vec2 snapZoomToPowerOfTwo(IRMath::vec2 zoom) {
    return IRMath::vec2(resolveZoomRequest(zoom.x, false), resolveZoomRequest(zoom.y, false));
}

/// @p zoom times @p factor, clamped to the engine zoom range and left
/// unrounded: one continuous-policy zoom step.
inline IRMath::vec2 scaleZoomUnsnapped(IRMath::vec2 zoom, float factor) {
    return IRMath::clamp(
        zoom * factor,
        IRConstants::kTrixelCanvasZoomMin,
        IRConstants::kTrixelCanvasZoomMax
    );
}

/// Whether the camera accepts any zoom in the engine range. False when the
/// camera entity is not wired.
inline bool isZoomContinuous() {
    if (auto *camera = detail::cameraComponent<IRComponents::C_Camera>())
        return camera->continuousZoom_;
    return false;
}

/// Switch the camera's zoom policy. Either direction drops the carried raster
/// phase, so the next prepared frame starts a fresh history. Enabling keeps
/// the stored zoom; disabling snaps each stored axis to a power of two, so
/// the snapped invariant holds from this call on rather than from the next
/// zoom write. Call at a frame boundary.
inline void setZoomContinuous(bool continuous) {
    auto *camera = detail::cameraComponent<IRComponents::C_Camera>();
    if (camera == nullptr || camera->continuousZoom_ == continuous)
        return;
    camera->continuousZoom_ = continuous;
    if (auto *state = detail::cameraComponent<IRComponents::C_CameraZoomFrameState>())
        *state = IRComponents::C_CameraZoomFrameState{};
    if (continuous)
        return;
    if (auto *zoom = detail::cameraComponent<IRComponents::C_ZoomLevel>())
        zoom->zoom_ = snapZoomToPowerOfTwo(zoom->zoom_);
}

/// One camera zoom step in. Snapped policy: `C_ZoomLevel::zoomIn`, unchanged.
/// Continuous policy: doubles the stored zoom and clamps, with no rounding.
inline void zoomIn() {
    auto *zoom = detail::cameraComponent<IRComponents::C_ZoomLevel>();
    if (zoom == nullptr)
        return;
    if (isZoomContinuous())
        zoom->zoom_ = scaleZoomUnsnapped(zoom->zoom_, 2.0f);
    else
        zoom->zoomIn();
}

/// One camera zoom step out; the mirror of @ref zoomIn.
inline void zoomOut() {
    auto *zoom = detail::cameraComponent<IRComponents::C_ZoomLevel>();
    if (zoom == nullptr)
        return;
    if (isZoomContinuous())
        zoom->zoom_ = scaleZoomUnsnapped(zoom->zoom_, 0.5f);
    else
        zoom->zoomOut();
}

/// Publish this frame's continuous-zoom placement sample for the camera at
/// (@p effectiveCameraIso, @p zoom), advancing the carried phase from the
/// previous frame's sample. Under the snapped policy it clears the state
/// instead.
///
/// One caller, once per rendered frame: `TRIXEL_TO_FRAMEBUFFER::beginTick`,
/// after the camera controls and ahead of every stage that places
/// camera-following content. A second call in a frame with the same arguments
/// republishes the same sample.
inline void prepareZoomFrame(IRMath::vec2 effectiveCameraIso, IRMath::vec2 zoom) {
    auto *state = detail::cameraComponent<IRComponents::C_CameraZoomFrameState>();
    if (state == nullptr)
        return;
    if (!isZoomContinuous()) {
        *state = IRComponents::C_CameraZoomFrameState{};
        return;
    }
    state->sample_ =
        state->published_
            ? IRMath::advanceCameraRasterPhase(state->sample_, effectiveCameraIso, zoom)
            : IRMath::CameraRasterPhase{
                  IRMath::dvec2(effectiveCameraIso),
                  IRMath::cameraZoomPitch(zoom),
                  IRMath::dvec2(0.0)
              };
    state->published_ = true;
}

/// The placement sample published for the frame being drawn, or nullptr when
/// a stage must use the snapped placement: the policy is off, no frame has
/// been prepared, or the published sample is for a different camera pose than
/// the (@p effectiveCameraIso, @p zoom) the caller is about to place with.
/// That last case is a pipeline with no `TRIXEL_TO_FRAMEBUFFER` ahead of the
/// caller, or a camera write between the two.
///
/// The pointer is into component storage; copy the sample out, never hold it
/// past the tick.
inline const IRMath::CameraRasterPhase *
zoomFrame(IRMath::vec2 effectiveCameraIso, IRMath::vec2 zoom) {
    if (!isZoomContinuous())
        return nullptr;
    const auto *state = detail::cameraComponent<IRComponents::C_CameraZoomFrameState>();
    if (state == nullptr || !state->published_)
        return nullptr;
    if (state->sample_.cameraIso_ != IRMath::dvec2(effectiveCameraIso) ||
        state->sample_.pitch_ != IRMath::cameraZoomPitch(zoom))
        return nullptr;
    return &state->sample_;
}

/// Screen-pixel residual the framebuffer-to-screen upscale adds for the camera
/// at (@p effectiveCameraIso, @p zoom): the published sample's under the
/// continuous policy (@ref zoomFrame), the snapped decomposition's otherwise.
/// The upscale blit and every screen-space stage that aligns to its pixel grid
/// read this one value.
inline IRMath::ivec2
screenResidual(IRMath::vec2 effectiveCameraIso, IRMath::vec2 zoom, IRMath::ivec2 scaleFactor) {
    if (const IRMath::CameraRasterPhase *frame = zoomFrame(effectiveCameraIso, zoom))
        return IRMath::cameraRasterScreenResidual(*frame, scaleFactor);
    return IRMath::cameraSubPixelOffsets(effectiveCameraIso, zoom, scaleFactor).screenPxResidual_;
}

} // namespace IRPrefab::Camera

#endif /* IR_PREFAB_CAMERA_H */
