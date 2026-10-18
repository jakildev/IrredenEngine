#ifndef GLITCH_FIXTURE_H
#define GLITCH_FIXTURE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/glitch.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <array>
#include <cstdint>
#include <string_view>

// `--glitch-test <mode>`: the framebuffer glitch pass's evidence fixture.
//
// `standard-off` registers FRAMEBUFFER_GLITCH with its state off under the
// standing shot table, so its captures must equal the default references.
// Every other mode captures one `glitch_*` shot of the standard scene plus a
// cube that moves one voxel per engine tick, at fixed seed and phase: `off` is
// the control, each effect mode enables one effect, `hold-divisor` re-samples
// the hold history twice per period, and `all` enables every effect at a
// one-frame period (a history copy every frame) for profiling.
namespace IRShapeDebug {

struct GlitchFixture {
    enum class Mode {
        NONE,
        STANDARD_OFF,
        OFF,
        TEAR,
        HOLD,
        SPLIT,
        BARS,
        CELLS,
        DEPTH_TEAR,
        DEPTH_HOLD,
        HOLD_DIVISOR,
        ALL,
    };

    struct ModeInfo {
        Mode mode_;
        const char *arg_;
        const char *label_;
    };

    static constexpr std::array<ModeInfo, 12> kModes{{
        {Mode::NONE, "none", ""},
        {Mode::STANDARD_OFF, "standard-off", ""},
        {Mode::OFF, "off", "glitch_off"},
        {Mode::TEAR, "tear", "glitch_tear"},
        {Mode::HOLD, "hold", "glitch_hold"},
        {Mode::SPLIT, "split", "glitch_split"},
        {Mode::BARS, "bars", "glitch_bars"},
        {Mode::CELLS, "cells", "glitch_cells"},
        {Mode::DEPTH_TEAR, "depth-tear", "glitch_depth_tear"},
        {Mode::DEPTH_HOLD, "depth-hold", "glitch_depth_hold"},
        {Mode::HOLD_DIVISOR, "hold-divisor", "glitch_hold_divisor2"},
        {Mode::ALL, "all", "glitch_all"},
    }};

    // Period ticks start at engine tick 10, once the scene has drawn, and
    // last 60 ticks, so the capture (warmup 40 + settle) lands in period
    // tick 0. Hold re-samples at tick 10; with `holdDivisor = 2` again at
    // tick 40, so the two hold captures show the mover at different ages.
    static constexpr double kPeriodSeconds = 1.0;
    static constexpr double kPhaseSeconds = 10.0 / 60.0;
    static constexpr std::uint32_t kSeed = 7u;
    static constexpr IRMath::vec3 kMoverStart{-12.0f, 8.0f, -6.0f};
    static constexpr int kMoverSteps = 24;

    static const ModeInfo &info(std::string_view arg) {
        for (const ModeInfo &modeInfo : kModes) {
            if (arg == modeInfo.arg_) {
                return modeInfo;
            }
        }
        return kModes[0];
    }

    // A seed whose period tick 0 selects @p mode, so each depth capture is
    // pinned to the mode it names.
    static std::uint32_t seedForDepthMode(IRPrefab::Glitch::DepthMode mode) {
        std::uint32_t seed = kSeed;
        while (IRPrefab::Glitch::depthMode(seed, IRPrefab::Glitch::toTickWords(0)) != mode) {
            ++seed;
        }
        return seed;
    }

    static IRComponents::C_GlitchState state(Mode mode) {
        IRComponents::C_GlitchState state;
        state.periodSeconds_ = kPeriodSeconds;
        state.phaseSeconds_ = kPhaseSeconds;
        state.seed_ = kSeed;
        if (mode == Mode::NONE || mode == Mode::STANDARD_OFF || mode == Mode::OFF) {
            return state;
        }
        state.intensity_ = 1.0f;
        switch (mode) {
        case Mode::TEAR:
            state.tearWeight_ = 1.0f;
            break;
        case Mode::HOLD:
            state.holdWeight_ = 1.0f;
            break;
        case Mode::SPLIT:
            state.splitWeight_ = 1.0f;
            break;
        case Mode::BARS:
            state.barsWeight_ = 1.0f;
            break;
        case Mode::CELLS:
            state.cellsWeight_ = 1.0f;
            break;
        case Mode::DEPTH_TEAR:
            state.depthWeight_ = 1.0f;
            state.seed_ = seedForDepthMode(IRPrefab::Glitch::DepthMode::TEAR);
            break;
        case Mode::DEPTH_HOLD:
            state.depthWeight_ = 1.0f;
            state.seed_ = seedForDepthMode(IRPrefab::Glitch::DepthMode::HOLD);
            break;
        case Mode::HOLD_DIVISOR:
            state.holdWeight_ = 1.0f;
            state.holdDivisor_ = 2;
            break;
        case Mode::ALL:
            state.periodSeconds_ = 1.0 / 60.0;
            state.phaseSeconds_ = 0.0;
            state.tearWeight_ = 1.0f;
            state.holdWeight_ = 1.0f;
            state.splitWeight_ = 1.0f;
            state.barsWeight_ = 1.0f;
            state.cellsWeight_ = 1.0f;
            state.depthWeight_ = 1.0f;
            break;
        default:
            break;
        }
        return state;
    }

    // One single-shot table per capturing mode.
    static const IRVideo::AutoScreenshotShot *shot(Mode mode) {
        static const std::array<IRVideo::AutoScreenshotShot, kModes.size()> shots = [] {
            std::array<IRVideo::AutoScreenshotShot, kModes.size()> table{};
            for (std::size_t i = 0; i < kModes.size(); ++i) {
                table[i] = {4.0f, IRMath::vec2(0.0f), 0.0f, kModes[i].label_};
            }
            return table;
        }();
        for (std::size_t i = 0; i < kModes.size(); ++i) {
            if (kModes[i].mode_ == mode) {
                return &shots[i];
            }
        }
        return nullptr;
    }

    // The mover's position is a function of the engine tick alone, so every
    // run places it identically on every frame.
    static IRMath::vec3 moverPosition() {
        const auto step =
            static_cast<int>(IRTime::tick() % static_cast<std::uint64_t>(kMoverSteps));
        return kMoverStart + IRMath::vec3(static_cast<float>(step), 0.0f, 0.0f);
    }

    static IREntity::EntityId spawnMover() {
        return IREntity::createEntity(
            IRComponents::C_LocalTransform{kMoverStart},
            IRComponents::C_VoxelSetNew{
                IRMath::ivec3(3, 3, 3),
                IRMath::Color{240, 90, 60, 255},
                true
            }
        );
    }

    static void logProbe(const char *when) {
        const auto *glitch = IRPrefab::Glitch::systemOrNull();
        const IRSystem::SystemId screenId = IRSystem::findSystem(IRSystem::FRAMEBUFFER_TO_SCREEN);
        if (glitch == nullptr || screenId == IRSystem::kNullSystemId) {
            return;
        }
        const auto *screen =
            IRSystem::getSystemParams<IRSystem::System<IRSystem::FRAMEBUFFER_TO_SCREEN>>(screenId);
        const IRComponents::C_GlitchState current = IRPrefab::Glitch::stateOrDefault();
        const IRPrefab::Glitch::Clock clock = IRPrefab::Glitch::clock();
        const IRMath::ivec2 trixelStep =
            IRPrefab::Glitch::trixelStepForZoom(IRRender::getCameraZoom());
        const auto words = IRPrefab::Glitch::toTickWords(clock.tick_);
        IR_LOG_INFO(
            "[glitch-probe] at={} enabled={} tick={} holdTick={} trixelStep={}x{} "
            "frameDraws={} frameHistoryCopies={} totalDraws={} totalHistoryCopies={} "
            "ordinaryBlits={} gpuMs={:.4f} depthMode={}",
            when,
            IRPrefab::Glitch::isEnabled(current),
            clock.tick_,
            clock.holdTick_,
            trixelStep.x,
            trixelStep.y,
            glitch->frameDraws_,
            glitch->frameHistoryCopies_,
            glitch->drawCount_,
            glitch->historyCopyCount_,
            screen->ordinaryBlitCount_,
            IRRender::gpuStageTiming().framebufferGlitchMs_,
            IRPrefab::Glitch::depthMode(current.seed_, words) == IRPrefab::Glitch::DepthMode::TEAR
                ? "tear"
                : "hold"
        );
        const IRPrefab::Glitch::FrameTables tables = IRPrefab::Glitch::frameTables(
            IRPrefab::Glitch::frameParams(
                current,
                clock,
                trixelStep,
                IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer")
                    .getResolutionPlusBuffer()
            )
        );
        for (int band = 0; band < tables.tearCount_; ++band) {
            IR_LOG_INFO(
                "[glitch-probe] tearBand={} rows=[{},{}) offsetPx={} offsetTrixels={}",
                band,
                tables.tear_[band].start_,
                tables.tear_[band].end_,
                tables.tear_[band].offsetPx_,
                tables.tear_[band].offsetPx_ / trixelStep.x
            );
        }
        if (tables.depthEnabled_) {
            IR_LOG_INFO(
                "[glitch-probe] depth probeStart={} halfWidth={:.6f} tearPx={} holdBlend={:.3f}",
                tables.depthProbeStart_,
                tables.depthHalfWidth_,
                tables.depthTearPx_,
                tables.depthHoldBlend_
            );
        }
    }
};

} // namespace IRShapeDebug

#endif /* GLITCH_FIXTURE_H */
