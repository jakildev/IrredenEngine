#ifndef IR_VIDEO_AUTO_SCREENSHOT_H
#define IR_VIDEO_AUTO_SCREENSHOT_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/system/ir_system_types.hpp>
#include <irreden/input/ir_input_types.hpp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <vector>

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
    // Explicit camera Z-yaw pivot focus (#1921): when @c hasPivotFocus_ is set,
    // the shot calls @c IRRender::setRotationPivotFocus(pivotFocusWorld_) before
    // it settles, so the focus world point stays pinned across the shot's yaw —
    // a regression shot can prove tall / off-center content rotates in place.
    // Default (@c false) clears the focus, keeping every existing shot table
    // byte-identical to the pre-#1921 screen-center pivot.
    vec3 pivotFocusWorld_ = vec3(0.0f);
    bool hasPivotFocus_ = false;
};

/// Declarative config for @c createAutoScreenshotSystem. @c shots_ /
/// @c numShots_ point at a shot table owned by the caller — it must outlive
/// the game loop.
///
/// @c onCaptureFrame_ is an optional per-shot hook fired exactly once per shot,
/// on the settled capture frame, immediately after the screenshot is requested,
/// passing the shot index. It lets a creation record render state that only the
/// settled frame reflects — e.g. perf_grid's rotated-solidity harness samples
/// which render path (single-canvas cardinal vs per-axis) actually drew the
/// pose, so a "cardinal" row is unambiguous (#1882). State that engine/video
/// cannot see is injected as a type-erased callback (same pattern as
/// @c GuiTestConfig::onAssertFrame_). @c nullptr keeps every existing caller
/// byte-identical.
struct AutoScreenshotConfig {
    int warmupFrames_ = 10;
    int settleFrames_ = 3;
    const AutoScreenshotShot *shots_ = nullptr;
    int numShots_ = 0;
    void (*onCaptureFrame_)(int shotIndex) = nullptr;
};

/// Owns the label storage + shot vector for a runtime-computed indexed shot
/// sweep — a numbered walk over zoom/pan/yaw computed from a per-index
/// callback (a boundary pan, a spin-yaw walk, a zoom×yaw×pan matrix). @c
/// AutoScreenshotShot::label_ is a non-owning @c const @c char*, so the label
/// text must be stored somewhere that outlives the shots it's pointed at by
/// — this struct keeps the label storage and the shot table together so a
/// caller can't build one without the other.
///
/// @c LabelSize is the fixed per-shot label buffer width — pick the smallest
/// power-of-two-ish size that fits the longest label your @c labelFn writes
/// (snprintf truncates silently on overflow).
template <std::size_t LabelSize = 40> struct IndexedSweepShots {
    std::vector<AutoScreenshotShot> shots_;
    std::vector<std::array<char, LabelSize>> labels_;

    /// Builds @c n shots. @c shotFn(i) returns the shot's zoom/pan/yaw/etc.
    /// (its @c label_ is ignored and overwritten). @c labelFn(i, buf, size)
    /// writes the shot's label into @c buf via @c std::snprintf.
    template <typename ShotFn, typename LabelFn>
    void build(std::size_t n, ShotFn &&shotFn, LabelFn &&labelFn) {
        shots_.reserve(n);
        labels_.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            auto &label = labels_.emplace_back();
            labelFn(i, label.data(), label.size());
            AutoScreenshotShot shot = shotFn(i);
            shot.label_ = label.data();
            shots_.push_back(shot);
        }
    }
};

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

/// One scripted input event within a @c GuiTestShot.
///
/// @c frameOffset_ is relative to the first frame after the camera is applied
/// for this shot. Events at the same offset fire in array order. Settle frames
/// begin after the highest @c frameOffset_ in the list, ensuring the GUI
/// reflects the interaction before capture.
struct GuiInputEvent {
    int frameOffset_ = 0;
    enum class Type { MOVE, PRESS, RELEASE, SCROLL } type_ = Type::MOVE;
    ivec2 screenPx_ = ivec2(0);
    vec2 scroll_ = vec2(0.0f);
    IRInput::KeyMouseButtons button_ = IRInput::kNullButton;
};

/// Superset of @c AutoScreenshotShot — carries the same render config plus an
/// optional scripted input sequence. @c inputs_ / @c numInputs_ must outlive
/// the game loop. Pass @c nullptr / 0 for a pure render shot.
struct GuiTestShot {
    AutoScreenshotShot render_;
    const GuiInputEvent *inputs_ = nullptr;
    int numInputs_ = 0;
};

/// Declarative config for @c createGuiTestSystem.
///
/// @c onAssertFrame_ is the Phase 3 (#1796) assertion hook. When set, the
/// harness calls it once per frame while a shot is live (from the first frame
/// after the camera is applied through the capture frame), passing the shot
/// index and whether this is the capture frame. Assertion evaluation needs
/// widget / picking state that engine/video cannot see, so it is injected as a
/// type-erased callback the creation supplies (typically forwarding to
/// @c IRPrefab::GuiTest::onFrame). @c nullptr keeps a pure-capture run — every
/// existing GuiTest caller is unaffected.
struct GuiTestConfig {
    int warmupFrames_ = 10;
    int settleFrames_ = 3;
    const GuiTestShot *shots_ = nullptr;
    int numShots_ = 0;
    void (*onAssertFrame_)(int shotIndex, bool isCaptureFrame) = nullptr;
};

/// Create a system that fires scripted input events on scheduled frames,
/// settles for @c settleFrames_ frames after the last event per shot, captures
/// a screenshot, then calls @c IRWindow::closeWindow() when done.
///
/// Calls @c IRInput::beginSyntheticInput() at creation — the process is in
/// synthetic-input mode for its lifetime. Requires @c IREngine::init() has run.
IRSystem::SystemId createGuiTestSystem(const GuiTestConfig &config);

} // namespace IRVideo

#endif /* IR_VIDEO_AUTO_SCREENSHOT_H */
