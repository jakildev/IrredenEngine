#include <irreden/video/auto_screenshot.hpp>

#include <irreden/ir_input.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/cull_viewport_state.hpp>

#include <chrono>
#include <memory>

namespace IRVideo {

namespace {

// Private anchors: never attached to any entity, so the per-entity tick
// runs zero times. engine/system/CLAUDE.md guarantees endTick fires even
// when zero entities match — we only care about endTick here.
struct C_AutoScreenshotAnchor {};
struct C_GuiTestAnchor {};
struct C_AutoRecordAnchor {};

void logCaptureCameraState(const char *label) {
    const vec2 cameraIso = IRRender::getEffectiveCameraIso();
    const vec2 zoom = IRRender::getCameraZoom();
    const vec2 resolution = IRRender::getGameResolution();
    IR_LOG_INFO(
        "CaptureCamera: label={} yaw={} zoom=({},{}) effectiveIso=({},{}) "
        "subdivisions={} gameResolution=({},{})",
        label,
        IRPrefab::Camera::getYaw(),
        zoom.x,
        zoom.y,
        cameraIso.x,
        cameraIso.y,
        IRRender::getVoxelRenderEffectiveSubdivisions(),
        resolution.x,
        resolution.y
    );
}

// Apply one shot's camera state (zoom / pan / Z-yaw / pivot focus / cull
// freeze) before the settle window. Shared by the auto-screenshot and GUI-test
// cyclers — `GuiTestShot::render_` is an AutoScreenshotShot — so the per-shot
// pivot-focus and cull-freeze handling can't drift between them.
void applyShotCameraState(const AutoScreenshotShot &shot) {
    IRRender::setCameraZoom(shot.zoom_);
    IRRender::setCameraPosition2DIso(shot.cameraIso_);
    IRPrefab::Camera::setYaw(shot.yawRadians_);
    // Set or clear the explicit Z-yaw pivot point of interest so a regression
    // shot can pin tall / off-center content; clearing keeps shots with no
    // focus on the legacy screen-center pivot.
    if (shot.hasPivotFocus_) {
        IRRender::setRotationPivotFocus(shot.pivotFocusWorld_);
    } else {
        IRRender::clearRotationPivotFocus();
    }
    // FREEZE pins the cull viewport at this shot's pose (snapshotted on the next
    // updateCullViewport, within the settle frames) so later shots can free-fly
    // with the cull held; UNFREEZE returns to live tracking; NONE leaves it.
    if (shot.cullAction_ == CullAction::FREEZE) {
        IRRender::setCullingFrozen(true);
    } else if (shot.cullAction_ == CullAction::UNFREEZE) {
        IRRender::setCullingFrozen(false);
    }
}

struct CyclingState {
    AutoScreenshotConfig config_;
    int warmupRemaining_ = 0;
    int currentShot_ = 0;
    int settleCounter_ = 0;
    bool screenshotPending_ = false;
};

// Capture registration and pacing requests are process-lifetime state because
// capture systems are one-shot per process. World resolves the flags once,
// before entering its loop.
bool g_autoCaptureActive = false;
bool g_deterministicCaptureActive = false;
bool g_autoRecordActive = false;
bool g_autoRecordRealTime = false;
bool g_autoRecordRealTimeRequested = false;

detail::AutoCapturePacing currentAutoCapturePacing() {
    return detail::autoCapturePacingFrom(
        g_autoRecordActive,
        g_deterministicCaptureActive,
        g_autoRecordRealTime,
        g_autoRecordRealTimeRequested,
        IRVideo::isAudioInputArmed()
    );
}

const char *pacingDescription(detail::AutoCapturePacing pacing) {
    switch (pacing) {
    case detail::AutoCapturePacing::FIXED_STEP:
        return "fixed-step";
    case detail::AutoCapturePacing::FIXED_STEP_DETERMINISTIC_CAPTURE:
        return "fixed-step (deterministic capture armed)";
    case detail::AutoCapturePacing::REAL_TIME_REQUESTED:
        return "real-time (requested)";
    case detail::AutoCapturePacing::REAL_TIME_AUDIO_INPUT:
        return "real-time (audio input armed)";
    }
    return "fixed-step";
}

} // namespace

bool isAutoCaptureActive() {
    return g_autoCaptureActive;
}

bool isAutoCaptureFixedStep() {
    return detail::usesFixedStep(currentAutoCapturePacing());
}

void requestAutoRecordRealTime() {
    g_autoRecordRealTimeRequested = true;
}

IRSystem::SystemId createAutoScreenshotSystem(const AutoScreenshotConfig &config) {
    g_autoCaptureActive = true;
    g_deterministicCaptureActive = true;
    auto state = std::make_shared<CyclingState>();
    state->config_ = config;
    state->warmupRemaining_ = config.warmupFrames_;

    return IRSystem::createSystem<C_AutoScreenshotAnchor>(
        "AutoScreenshot",
        [](C_AutoScreenshotAnchor &) {},
        nullptr,
        [state]() {
            if (state->warmupRemaining_ > 0) {
                --state->warmupRemaining_;
                return;
            }

            if (state->currentShot_ >= state->config_.numShots_) {
                IRWindow::closeWindow();
                return;
            }

            if (state->settleCounter_ == 0) {
                const auto &shot = state->config_.shots_[state->currentShot_];
                IR_LOG_INFO(
                    "AutoScreenshot {}/{}: {} (zoom={}, cam=({},{}), yaw={}, cull={})",
                    state->currentShot_ + 1,
                    state->config_.numShots_,
                    shot.label_,
                    shot.zoom_,
                    shot.cameraIso_.x,
                    shot.cameraIso_.y,
                    shot.yawRadians_,
                    static_cast<int>(shot.cullAction_)
                );
                applyShotCameraState(shot);
                state->settleCounter_ = state->config_.settleFrames_;
                state->screenshotPending_ = false;
                return;
            }

            if (state->settleCounter_ > 1) {
                --state->settleCounter_;
                return;
            }

            if (!state->screenshotPending_) {
                const auto &shot = state->config_.shots_[state->currentShot_];
                logCaptureCameraState(shot.label_);
                if (shot.numCrops_ > 0 && shot.crops_ != nullptr) {
                    IRVideo::requestScreenshotWithCrops(shot.label_, shot.crops_, shot.numCrops_);
                } else {
                    IRVideo::requestScreenshot();
                }
                // Per-shot capture hook: fire once on the settled frame
                // so the caller can record render state the capture reflects.
                if (state->config_.onCaptureFrame_ != nullptr) {
                    state->config_.onCaptureFrame_(state->currentShot_);
                }
                state->screenshotPending_ = true;
                return;
            }

            state->settleCounter_ = 0;
            state->screenshotPending_ = false;
            ++state->currentShot_;
        }
    );
}

IRSystem::SystemId createAutoRecordSystem(const AutoRecordConfig &config) {
    g_autoCaptureActive = true;
    g_autoRecordActive = true;
    g_autoRecordRealTime = config.realTime_;

    struct RecordState {
        AutoRecordConfig config_;
        int warmupRemaining_ = 0;
        std::int64_t seenTicks_ = 0;
        std::chrono::steady_clock::time_point startedAt_{};
        enum class Phase { WARMUP, STARTING, RECORDING, STOPPING, DONE } phase_ = Phase::WARMUP;
    };
    using Phase = RecordState::Phase;

    auto state = std::make_shared<RecordState>();
    state->config_ = config;
    state->warmupRemaining_ = config.warmupFrames_;

    // A toggle requested here lands in VideoManager::render() later in the
    // same frame, after the RENDER pipeline. The frame after the start
    // request is therefore the first the recorder sees, and the stop request
    // is processed before that frame's own capture — so counting from the
    // first recorded frame and stopping on the (frames_ + 1)th endTick
    // submits exactly frames_ captures.
    return IRSystem::createSystem<C_AutoRecordAnchor>(
        "AutoRecord",
        [](C_AutoRecordAnchor &) {},
        nullptr,
        [state]() {
            const auto exitWithRecorderError = [&state](const char *reason) {
                IR_LOG_WARN(
                    "AutoRecord: {} ({}); exiting without a clip",
                    reason,
                    IRVideo::getLastError()
                );
                IRWindow::closeWindow();
                state->phase_ = Phase::DONE;
            };
            switch (state->phase_) {
            case Phase::WARMUP:
                if (state->warmupRemaining_ > 0) {
                    --state->warmupRemaining_;
                    return;
                }
                IR_LOG_INFO(
                    "AutoRecord: starting capture, {} tick window, pacing={}",
                    state->config_.frames_,
                    pacingDescription(currentAutoCapturePacing())
                );
                state->startedAt_ = std::chrono::steady_clock::now();
                IRVideo::toggleRecording();
                state->phase_ = Phase::STARTING;
                return;
            case Phase::STARTING:
                if (IRVideo::recordingState() != RecordingState::RECORDING) {
                    exitWithRecorderError("recorder did not start");
                    return;
                }
                state->phase_ = Phase::RECORDING;
                [[fallthrough]];
            case Phase::RECORDING: {
                if (IRVideo::recordingState() != RecordingState::RECORDING) {
                    exitWithRecorderError("recorder stopped unexpectedly");
                    return;
                }
                const detail::AutoRecordWindowStep window = detail::autoRecordWindowStep(
                    state->seenTicks_,
                    IRVideo::capturedUpdateTicks(),
                    state->config_.frames_
                );
                state->seenTicks_ = window.seenTicks_;
                if (!window.stop_) {
                    return;
                }
                IR_LOG_INFO(
                    "AutoRecord: stopping, ticks={} wall={:.2f}s",
                    state->seenTicks_,
                    std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - state->startedAt_
                    )
                        .count()
                );
                IRVideo::toggleRecording();
                state->phase_ = Phase::STOPPING;
                return;
            }
            case Phase::STOPPING:
                IRWindow::closeWindow();
                state->phase_ = Phase::DONE;
                return;
            case Phase::DONE:
                return;
            }
        }
    );
}

IRSystem::SystemId createGuiTestSystem(const GuiTestConfig &config) {
    g_autoCaptureActive = true;
    g_deterministicCaptureActive = true;
    IRInput::beginSyntheticInput();

    struct GuiTestState {
        GuiTestConfig config_;
        int warmupRemaining_ = 0;
        int currentShot_ = 0;
        // -1 = camera not applied yet for this shot; 0+ = frames since camera apply
        int shotFrame_ = -1;
        bool screenshotRequested_ = false;
    };

    auto state = std::make_shared<GuiTestState>();
    state->config_ = config;
    state->warmupRemaining_ = config.warmupFrames_;

    return IRSystem::createSystem<C_GuiTestAnchor>(
        "GuiTest",
        [](C_GuiTestAnchor &) {},
        nullptr,
        [state]() {
            if (state->warmupRemaining_ > 0) {
                --state->warmupRemaining_;
                return;
            }
            if (state->currentShot_ >= state->config_.numShots_) {
                IRWindow::closeWindow();
                return;
            }

            const auto &shot = state->config_.shots_[state->currentShot_];

            if (state->shotFrame_ < 0) {
                IR_LOG_INFO(
                    "GuiTest {}/{}: {} (zoom={}, cam=({},{}), yaw={}, inputs={})",
                    state->currentShot_ + 1,
                    state->config_.numShots_,
                    shot.render_.label_,
                    shot.render_.zoom_,
                    shot.render_.cameraIso_.x,
                    shot.render_.cameraIso_.y,
                    shot.render_.yawRadians_,
                    shot.numInputs_
                );
                applyShotCameraState(shot.render_);
                state->shotFrame_ = 0;
                return;
            }

            int maxOffset = -1;
            for (int i = 0; i < shot.numInputs_; ++i)
                maxOffset = IRMath::max(maxOffset, shot.inputs_[i].frameOffset_);

            // captureAt: settleFrames_ full settle frames follow the last event.
            const int captureAt = maxOffset + 1 + state->config_.settleFrames_;

            // Fire the assertion hook every live frame so the
            // consumer can latch one-frame pulses (C_WidgetState::fireAction_);
            // flag the capture frame for evaluation. Gated on
            // !screenshotRequested_ so the capture frame fires it exactly once
            // (the request tick), not again on the advance tick.
            if (state->config_.onAssertFrame_ != nullptr && !state->screenshotRequested_) {
                state->config_.onAssertFrame_(state->currentShot_, state->shotFrame_ == captureAt);
            }

            // Event phase: dispatch any events scheduled for shotFrame_.
            if (state->shotFrame_ <= maxOffset) {
                for (int i = 0; i < shot.numInputs_; ++i) {
                    const auto &ev = shot.inputs_[i];
                    if (ev.frameOffset_ != state->shotFrame_)
                        continue;
                    switch (ev.type_) {
                    case GuiInputEvent::Type::MOVE:
                        IRInput::injectMouseMove(ev.screenPx_);
                        break;
                    case GuiInputEvent::Type::PRESS:
                        IRInput::injectButton(ev.button_, IRInput::ButtonStatuses::PRESSED);
                        break;
                    case GuiInputEvent::Type::RELEASE:
                        IRInput::injectButton(ev.button_, IRInput::ButtonStatuses::RELEASED);
                        break;
                    case GuiInputEvent::Type::SCROLL:
                        IRInput::injectScroll(
                            static_cast<double>(ev.scroll_.x),
                            static_cast<double>(ev.scroll_.y)
                        );
                        break;
                    }
                }
                ++state->shotFrame_;
                return;
            }

            // Settle phase: wait until captureAt.
            if (state->shotFrame_ < captureAt) {
                ++state->shotFrame_;
                return;
            }

            // Capture frame.
            if (!state->screenshotRequested_) {
                const auto &r = shot.render_;
                logCaptureCameraState(r.label_);
                if (r.numCrops_ > 0 && r.crops_ != nullptr)
                    IRVideo::requestScreenshotWithCrops(r.label_, r.crops_, r.numCrops_);
                else
                    IRVideo::requestScreenshot();
                state->screenshotRequested_ = true;
                return;
            }

            // Advance to next shot.
            ++state->currentShot_;
            state->shotFrame_ = -1;
            state->screenshotRequested_ = false;
        }
    );
}

} // namespace IRVideo
