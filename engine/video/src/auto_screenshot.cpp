#include <irreden/video/auto_screenshot.hpp>

#include <irreden/ir_input.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/cull_viewport_state.hpp>

#include <cstdlib>
#include <cstring>
#include <memory>

namespace IRVideo {

namespace {

// Private anchor: never attached to any entity, so the per-entity tick
// runs zero times. engine/system/CLAUDE.md guarantees endTick fires even
// when zero entities match — we only care about endTick here.
struct C_AutoScreenshotAnchor {};
struct C_GuiTestAnchor {};

struct CyclingState {
    AutoScreenshotConfig config_;
    int warmupRemaining_ = 0;
    int currentShot_ = 0;
    int settleCounter_ = 0;
    bool screenshotPending_ = false;
};

// Set true the first time createAutoScreenshotSystem runs this process, i.e. a
// headless --auto-screenshot capture is active. Read by World via
// isAutoCaptureActive() to switch the UPDATE loop to a deterministic fixed
// step. Process-lifetime flag; capture is one-shot per process.
bool g_autoCaptureActive = false;

} // namespace

bool parseAutoScreenshotArgv(int argc, char **argv, int *warmupFramesOut) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--auto-screenshot") != 0)
            continue;
        int warmup = 10;
        if (i + 1 < argc) {
            int parsed = std::atoi(argv[i + 1]);
            if (parsed > 0)
                warmup = parsed;
        }
        if (warmupFramesOut != nullptr)
            *warmupFramesOut = warmup;
        return true;
    }
    return false;
}

bool isAutoCaptureActive() {
    return g_autoCaptureActive;
}

IRSystem::SystemId createAutoScreenshotSystem(const AutoScreenshotConfig &config) {
    g_autoCaptureActive = true;
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
                IRRender::setCameraZoom(shot.zoom_);
                IRRender::setCameraPosition2DIso(shot.cameraIso_);
                IRPrefab::Camera::setYaw(shot.yawRadians_);
                // Drive the shared cull-freeze flag while the camera sits at
                // THIS shot's pose. FREEZE pins the cull here (the snapshot is
                // taken on the next IRRender::updateCullViewport, within the
                // settle frames); subsequent shots move the camera with the
                // cull held. NONE leaves the flag as-is.
                if (shot.cullAction_ == CullAction::FREEZE) {
                    IRRender::setCullingFrozen(true);
                } else if (shot.cullAction_ == CullAction::UNFREEZE) {
                    IRRender::setCullingFrozen(false);
                }
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
                if (shot.numCrops_ > 0 && shot.crops_ != nullptr) {
                    IRVideo::requestScreenshotWithCrops(shot.label_, shot.crops_, shot.numCrops_);
                } else {
                    IRVideo::requestScreenshot();
                }
                // Per-shot capture hook (#1882): fire once on the settled frame
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

IRSystem::SystemId createGuiTestSystem(const GuiTestConfig &config) {
    g_autoCaptureActive = true;
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
                IRRender::setCameraZoom(shot.render_.zoom_);
                IRRender::setCameraPosition2DIso(shot.render_.cameraIso_);
                IRPrefab::Camera::setYaw(shot.render_.yawRadians_);
                if (shot.render_.cullAction_ == CullAction::FREEZE) {
                    IRRender::setCullingFrozen(true);
                } else if (shot.render_.cullAction_ == CullAction::UNFREEZE) {
                    IRRender::setCullingFrozen(false);
                }
                state->shotFrame_ = 0;
                return;
            }

            int maxOffset = -1;
            for (int i = 0; i < shot.numInputs_; ++i)
                maxOffset = IRMath::max(maxOffset, shot.inputs_[i].frameOffset_);

            // captureAt: settleFrames_ full settle frames follow the last event.
            const int captureAt = maxOffset + 1 + state->config_.settleFrames_;

            // Phase 3 assertion hook (#1796): fire every live frame so the
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
