#include <irreden/video/auto_screenshot.hpp>

#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <cstdlib>
#include <cstring>
#include <memory>

namespace IRVideo {

namespace {

// Private anchor: never attached to any entity, so the per-entity tick
// runs zero times. engine/system/CLAUDE.md guarantees endTick fires even
// when zero entities match — we only care about endTick here.
struct C_AutoScreenshotAnchor {};

struct CyclingState {
    AutoScreenshotConfig config_;
    int warmupRemaining_ = 0;
    int currentShot_ = 0;
    int settleCounter_ = 0;
    bool screenshotPending_ = false;
};

} // namespace

bool parseAutoScreenshotArgv(int argc, char **argv, int *warmupFramesOut) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--auto-screenshot") != 0) continue;
        int warmup = 10;
        if (i + 1 < argc) {
            int parsed = std::atoi(argv[i + 1]);
            if (parsed > 0) warmup = parsed;
        }
        if (warmupFramesOut != nullptr) *warmupFramesOut = warmup;
        return true;
    }
    return false;
}

IRSystem::SystemId createAutoScreenshotSystem(const AutoScreenshotConfig &config) {
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
                    "AutoScreenshot {}/{}: {} (zoom={}, cam=({},{}))",
                    state->currentShot_ + 1, state->config_.numShots_,
                    shot.label_, shot.zoom_, shot.cameraIso_.x, shot.cameraIso_.y
                );
                IRRender::setCameraZoom(shot.zoom_);
                IRRender::setCameraPosition2DIso(shot.cameraIso_);
                state->settleCounter_ = state->config_.settleFrames_;
                state->screenshotPending_ = false;
                return;
            }

            if (state->settleCounter_ > 1) {
                --state->settleCounter_;
                return;
            }

            if (!state->screenshotPending_) {
                IRVideo::requestScreenshot();
                state->screenshotPending_ = true;
                return;
            }

            state->settleCounter_ = 0;
            state->screenshotPending_ = false;
            ++state->currentShot_;
        }
    );
}

} // namespace IRVideo
