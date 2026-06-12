#ifndef IR_VIDEO_AUTO_SCREENSHOT_H
#define IR_VIDEO_AUTO_SCREENSHOT_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/system/ir_system_types.hpp>

namespace IRVideo {

/// Optional sub-rectangle of a shot's framebuffer to dump as a small PNG
/// alongside the full-frame screenshot. Used by render-debug agents to
/// inspect pixel-level edge fidelity without re-rendering.
///
/// Coordinates are framebuffer pixels with the top-left origin used by
/// PNG viewers (matching what @c captureScreenshot writes). Out-of-bounds
/// rects are clamped; rects that clamp to zero area log a warning and skip.
///
/// @c label_ is deep-copied by @c VideoManager when the crop request is
/// queued — it need not outlive that call. The enclosing crop table
/// referenced by @c AutoScreenshotShot::crops_ still must outlive the
/// game loop.
struct RoiCrop {
    int x_ = 0;
    int y_ = 0;
    int w_ = 128;
    int h_ = 128;
    const char *label_ = "crop";
};

/// Cull-freeze action to apply when this shot is active.
enum class CullAction {
    NONE,
    FREEZE,
    UNFREEZE,
};

/// One entry in an auto-screenshot shot list. The cycling system applies
/// @c zoom_, @c cameraIso_, and @c yawRadians_ via @c IRRender /
/// @c IRPrefab::Camera before capture, waits the configured settle frames,
/// then triggers a composite screenshot.
///
/// @c yawRadians_ writes the camera's continuous Z-yaw before each shot via
/// @c IRPrefab::Camera::setYaw, letting a shot list cover rotation-variant
/// regressions (#1261) without per-demo wiring. Default 0 keeps existing
/// shot tables at the cardinal baseline.
///
/// @c label_ is printed to the log around each capture and — when @c crops_
/// is set — appears in each crop PNG's filename
/// (@c screenshot_<n>_<label>__crop_<crop_label>.png). The full-frame PNG
/// keeps the running-counter-only name for compatibility with downstream
/// scripts.
///
/// @c crops_ / @c numCrops_ point at a caller-owned table that must
/// outlive the game loop, same lifetime contract as @c shots_.
///
/// @c cullAction_ drives the shared cull-freeze state alongside the camera
/// params (#1438). @c NONE leaves the freeze flag untouched, so existing shot
/// tables are unaffected. @c FREEZE pins the cull viewport at THIS shot's
/// camera pose — the cycling system sets the flag while the camera sits here,
/// and @c IRRender::updateCullViewport snapshots the viewport on the next
/// frame; later shots can then move the camera while the cull stays pinned.
/// @c UNFREEZE returns to live cull tracking. Used to build a frozen-cull
/// free-fly sweep that proves the live cull retains the on-screen set.
struct AutoScreenshotShot {
    float zoom_ = 1.0f;
    vec2 cameraIso_ = vec2(0.0f);
    float yawRadians_ = 0.0f;
    const char *label_ = "shot";
    const RoiCrop *crops_ = nullptr;
    int numCrops_ = 0;
    CullAction cullAction_ = CullAction::NONE;
};

/// Declarative config for @c createAutoScreenshotSystem. @c shots_ /
/// @c numShots_ point at a shot table owned by the caller — it must outlive
/// the game loop.
struct AutoScreenshotConfig {
    int warmupFrames_ = 10;
    int settleFrames_ = 3;
    const AutoScreenshotShot *shots_ = nullptr;
    int numShots_ = 0;
};

/// Parse @c --auto-screenshot<space>[frames] from argv. Returns @c true if
/// the flag is present. When present and followed by a positive integer,
/// @c *warmupFramesOut is set to that value; otherwise the default of 10 is
/// written. @c warmupFramesOut may be @c nullptr.
///
/// This helper peeks at the token after @c --auto-screenshot but does not
/// tell the caller whether it consumed one or two argv slots. Callers with
/// subsequent argv loops must be aware that a bare positive integer may
/// appear at the slot immediately after @c --auto-screenshot — if their
/// own loop could interpret that token, skip past it explicitly.
bool parseAutoScreenshotArgv(int argc, char **argv, int *warmupFramesOut);

/// True once @c createAutoScreenshotSystem has been called this run — i.e. the
/// process is doing a headless auto-screenshot capture. @c World reads this to
/// switch the UPDATE loop to a deterministic fixed step (one tick per render
/// frame) so per-tick animation (AUTO_SPIN, etc.) advances reproducibly during
/// the frame-counted capture window instead of being starved by the uncapped
/// (vsync-off) headless loop.
bool isAutoCaptureActive();

/// Create a system that cycles through @c config.shots_ — one screenshot per
/// shot with @c settleFrames_ between shots — then calls
/// @c IRWindow::closeWindow(). The caller must append the returned @c SystemId
/// to the RENDER pipeline before @c registerPipeline fires.
///
/// Requires @c IREngine::init() has run (so the system manager is live).
IRSystem::SystemId createAutoScreenshotSystem(const AutoScreenshotConfig &config);

} // namespace IRVideo

#endif /* IR_VIDEO_AUTO_SCREENSHOT_H */
