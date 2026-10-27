#ifndef GLITCH_FIXTURE_H
#define GLITCH_FIXTURE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_sprite.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/framebuffer_screen_math.hpp>
#include <irreden/render/glitch.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// `--glitch-test <mode>`: the framebuffer glitch pass's evidence fixture.
//
// `standard-off` registers FRAMEBUFFER_GLITCH with its state off under the
// standing shot table, so its captures must equal the default references.
// Every other mode captures one `glitch_*` shot of the standard scene plus a
// cube that moves one voxel per engine tick, at fixed seed and phase: `off` is
// the control, each effect mode enables one effect, `hold-divisor` re-samples
// the hold history twice per period, and `all` enables every effect at a
// one-frame period (a history copy every frame) for profiling. Every capturing
// mode also spawns a sentinel sprite: `SPRITE_TO_SCREEN` draws after the pass,
// so its pixels must be identical in every capture.
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

    // Absent from the scene and from the bar palette, so a screen pixel of
    // this colour is the sentinel sprite.
    static constexpr std::array<std::uint8_t, 4> kSentinelRgba{37, 201, 173, 255};
    static constexpr IRMath::vec3 kSentinelPosition{0.0f, 0.0f, 0.0f};
    static constexpr IRMath::vec2 kSentinelSizePx{12.0f, 12.0f};

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

    static IREntity::EntityId spawnSentinel() {
        constexpr int kTexels = 2;
        auto [handle, texture] = IRRender::createResource<IRRender::Texture2D>(
            IRRender::TextureKind::TEXTURE_2D,
            static_cast<unsigned int>(kTexels),
            static_cast<unsigned int>(kTexels),
            IRRender::TextureFormat::RGBA8,
            IRRender::TextureWrap::CLAMP_TO_EDGE,
            IRRender::TextureFilter::NEAREST
        );
        std::array<std::uint8_t, kTexels * kTexels * 4> texels{};
        for (std::size_t i = 0; i < texels.size(); ++i) {
            texels[i] = kSentinelRgba[i % 4];
        }
        texture->subImage2D(
            0,
            0,
            kTexels,
            kTexels,
            IRRender::PixelDataFormat::RGBA,
            IRRender::PixelDataType::UNSIGNED_BYTE,
            texels.data()
        );
        return IREntity::createEntity(
            IRComponents::C_LocalTransform{kSentinelPosition},
            IRComponents::C_Sprite{handle, kSentinelSizePx}
        );
    }

    // The capture frame's screen target, compared pixel by pixel with the
    // unglitched composite texel the ordinary blit would have drawn there.
    // That texel is what `glitch_off` shows at the same pixel, so `changed_`
    // is the diff against the control; an `off` capture must report zero.
    struct ScreenCounts {
        bool valid_ = false;
        IRMath::ivec2 target_ = IRMath::ivec2(0);
        int compared_ = 0;
        int changed_ = 0;
        int sentinel_ = 0;
        IRMath::ivec4 sentinelRect_ = IRMath::ivec4(0);
        std::uint64_t sentinelHash_ = 0xCBF29CE484222325ull;
        IRPrefab::Glitch::DepthInterval interval_{};
        int inChanged_ = 0;
        int inUnchanged_ = 0;
        int outChanged_ = 0;
        int outUnchanged_ = 0;

        bool depthPasses() const {
            return inChanged_ > 0 && outChanged_ == 0 && outUnchanged_ > 0;
        }
    };

    static ScreenCounts compareScreen(const IRPrefab::Glitch::FrameTables &tables) {
        ScreenCounts counts;
        const auto &framebuffer =
            IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer");
        const IRMath::ivec2 size = framebuffer.getResolutionPlusBuffer();
        const auto texelCount = static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y);
        IRWindow::getFramebufferSize(counts.target_);
        if (counts.target_.x <= 0 || counts.target_.y <= 0) {
            return counts;
        }
        std::vector<std::uint8_t> screen(
            static_cast<std::size_t>(counts.target_.x) *
            static_cast<std::size_t>(counts.target_.y) * 4u
        );
        // Commits and waits: the colour and depth readbacks need this frame's
        // work complete.
        if (!IRRender::readDefaultFramebuffer(
                0,
                0,
                counts.target_.x,
                counts.target_.y,
                screen.data()
            )) {
            return counts;
        }
        std::vector<std::uint8_t> colour(texelCount * 4u);
        framebuffer.getTextureColor().getSubImage2D(
            0,
            0,
            size.x,
            size.y,
            IRRender::PixelDataFormat::RGBA,
            IRRender::PixelDataType::UNSIGNED_BYTE,
            colour.data()
        );
        std::vector<float> depth(texelCount);
        framebuffer.getTextureDepth().getSubImage2D(
            0,
            0,
            size.x,
            size.y,
            IRRender::PixelDataFormat::DEPTH_COMPONENT,
            IRRender::PixelDataType::FLOAT32,
            depth.data()
        );
        const auto texelIndex = [&size](IRMath::ivec2 texel) {
            return static_cast<std::size_t>(texel.y) * static_cast<std::size_t>(size.x) +
                   static_cast<std::size_t>(texel.x);
        };
        if (tables.depthEnabled_) {
            counts.interval_ = IRPrefab::Glitch::selectDepthInterval(
                tables,
                [&depth, &texelIndex](IRMath::ivec2 texel) { return depth[texelIndex(texel)]; }
            );
        }

        // Inverse of the screen pass: the quad `framebufferScreenModel` places,
        // its uv, and the texel its fragment reads — rows of uv.y on OpenGL,
        // of 1 - uv.y on Metal, whose vertex stage flips clip y.
        const IRMath::mat4 model = IRSystem::detail::framebufferScreenModel(
            size,
            IREntity::getComponent<IRComponents::C_WorldTransform>("mainFramebuffer").translation_,
            IRMath::vec2(
                IRPrefab::Camera::screenResidual(
                    IRRender::getEffectiveCameraIso(),
                    IRRender::getCameraZoom(),
                    IRRender::getOutputScaleFactor()
                )
            ),
            true
        );
        const IRMath::vec2 quadCenter(model[3].x, model[3].y);
        const IRMath::vec2 quadSize(model[0].x, model[1].y);
        const auto viewportHeight = static_cast<float>(IRRender::getViewport().y);
        counts.sentinelRect_ = IRMath::ivec4(counts.target_, -1, -1);
        for (int y = 0; y < counts.target_.y; ++y) {
            const float screenY = static_cast<float>(y) + 0.5f;
            const float orthoY = IRPlatform::kGfx.flipMouseY_ ? viewportHeight - screenY : screenY;
            for (int x = 0; x < counts.target_.x; ++x) {
                const IRMath::vec2 uv =
                    (IRMath::vec2(static_cast<float>(x) + 0.5f, orthoY) - quadCenter) / quadSize +
                    0.5f;
                if (uv.x < 0.0f || uv.y < 0.0f || uv.x >= 1.0f || uv.y >= 1.0f) {
                    continue;
                }
                const float rowCoord = IRPlatform::kIsOpenGL ? uv.y : 1.0f - uv.y;
                const IRMath::ivec2 texel = IRMath::clamp(
                    IRMath::ivec2(
                        static_cast<int>(IRMath::floor(uv.x * static_cast<float>(size.x))),
                        static_cast<int>(IRMath::floor(rowCoord * static_cast<float>(size.y)))
                    ),
                    IRMath::ivec2(0),
                    size - IRMath::ivec2(1)
                );
                const std::uint8_t *shot =
                    &screen
                        [(static_cast<std::size_t>(y) * static_cast<std::size_t>(counts.target_.x) +
                          static_cast<std::size_t>(x)) *
                         4u];
                if (shot[0] == kSentinelRgba[0] && shot[1] == kSentinelRgba[1] &&
                    shot[2] == kSentinelRgba[2]) {
                    ++counts.sentinel_;
                    counts.sentinelRect_ = IRMath::ivec4(
                        IRMath::min(counts.sentinelRect_.x, x),
                        IRMath::min(counts.sentinelRect_.y, y),
                        IRMath::max(counts.sentinelRect_.z, x),
                        IRMath::max(counts.sentinelRect_.w, y)
                    );
                    for (const int coordinate : {x, y}) {
                        counts.sentinelHash_ =
                            (counts.sentinelHash_ ^ static_cast<std::uint32_t>(coordinate)) *
                            0x100000001B3ull;
                    }
                    continue;
                }
                const std::uint8_t *control = &colour[texelIndex(texel) * 4u];
                const bool changed =
                    shot[0] != control[0] || shot[1] != control[1] || shot[2] != control[2];
                ++counts.compared_;
                counts.changed_ += changed ? 1 : 0;
                if (!tables.depthEnabled_) {
                    continue;
                }
                const bool inRange =
                    IRPrefab::Glitch::inDepthInterval(counts.interval_, depth[texelIndex(texel)]);
                if (inRange) {
                    ++(changed ? counts.inChanged_ : counts.inUnchanged_);
                } else {
                    ++(changed ? counts.outChanged_ : counts.outUnchanged_);
                }
            }
        }
        counts.valid_ = true;
        return counts;
    }

    static void logProbe(const char *when, bool compareScreenTarget) {
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
        const IRMath::ivec2 outputScale = IRRender::getOutputScaleFactor();
        const IRMath::ivec2 viewport = IRRender::getViewport();
        IR_LOG_INFO(
            "[glitch-probe] at={} enabled={} tick={} holdTick={} trixelStep={}x{} "
            "outputScale={}x{} viewport={}x{} "
            "frameDraws={} frameHistoryCopies={} totalDraws={} totalHistoryCopies={} "
            "ordinaryBlits={} glitchOutputBlits={} gpuMs={:.4f} depthMode={}",
            when,
            IRPrefab::Glitch::isEnabled(current),
            clock.tick_,
            clock.holdTick_,
            trixelStep.x,
            trixelStep.y,
            outputScale.x,
            outputScale.y,
            viewport.x,
            viewport.y,
            glitch->frameDraws_,
            glitch->frameHistoryCopies_,
            glitch->drawCount_,
            glitch->historyCopyCount_,
            screen->ordinaryBlitCount_,
            screen->glitchOutputBlitCount_,
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
        const ScreenCounts counts = compareScreenTarget ? compareScreen(tables) : ScreenCounts{};
        if (counts.valid_) {
            IR_LOG_INFO(
                "[glitch-probe] screen target={}x{} compared={} changed={} sentinelPx={} "
                "sentinelRect=[{},{},{},{}] sentinelHash={:016x}",
                counts.target_.x,
                counts.target_.y,
                counts.compared_,
                counts.changed_,
                counts.sentinel_,
                counts.sentinelRect_.x,
                counts.sentinelRect_.y,
                counts.sentinelRect_.z,
                counts.sentinelRect_.w,
                counts.sentinelHash_
            );
        }
        if (!tables.depthEnabled_) {
            return;
        }
        if (!counts.valid_) {
            IR_LOG_INFO(
                "[glitch-probe] depth probeStart={} halfWidth={:.6f} tearPx={} holdBlend={:.3f}",
                tables.depthProbeStart_,
                tables.depthHalfWidth_,
                tables.depthTearPx_,
                tables.depthHoldBlend_
            );
            return;
        }
        IR_LOG_INFO(
            "[glitch-probe] depth probeStart={} probe={} center={:.6f} halfWidth={:.6f} tearPx={} "
            "holdBlend={:.3f} inChanged={} inUnchanged={} outChanged={} outUnchanged={} pass={}",
            tables.depthProbeStart_,
            counts.interval_.probe_,
            counts.interval_.center_,
            tables.depthHalfWidth_,
            tables.depthTearPx_,
            tables.depthHoldBlend_,
            counts.inChanged_,
            counts.inUnchanged_,
            counts.outChanged_,
            counts.outUnchanged_,
            counts.depthPasses()
        );
    }
};

} // namespace IRShapeDebug

#endif /* GLITCH_FIXTURE_H */
