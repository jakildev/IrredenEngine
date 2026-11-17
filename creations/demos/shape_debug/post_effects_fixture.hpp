#ifndef POST_EFFECTS_FIXTURE_H
#define POST_EFFECTS_FIXTURE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/post_effects.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// `--post-effects-test <mode>`: the framebuffer post-effects pass's evidence
// fixture.
//
// `standard-off` registers FRAMEBUFFER_POST_EFFECTS with every effect off
// under the standing shot table, so its captures must equal the default
// references. Every other mode captures one `post_effects_*` shot of the
// standard scene: `off` is the control, `blur`, `bloom` and `grade` each
// enable one effect, and `all` enables the three together with the bloom
// screened instead of added. On the capture frame the probe reads the
// composite and the pass's output back and replays the frame through the CPU
// twin (`post_effects_math.hpp`).
namespace IRShapeDebug {

struct PostEffectsFixture {
    enum class Mode {
        NONE,
        STANDARD_OFF,
        OFF,
        BLUR,
        BLOOM,
        GRADE,
        ALL,
    };

    struct ModeInfo {
        Mode mode_;
        const char *arg_;
        const char *label_;
    };

    static constexpr std::array<ModeInfo, 7> kModes{{
        {Mode::NONE, "none", ""},
        {Mode::STANDARD_OFF, "standard-off", ""},
        {Mode::OFF, "off", "post_effects_off"},
        {Mode::BLUR, "blur", "post_effects_blur"},
        {Mode::BLOOM, "bloom", "post_effects_bloom"},
        {Mode::GRADE, "grade", "post_effects_grade"},
        {Mode::ALL, "all", "post_effects_all"},
    }};

    static const ModeInfo &info(std::string_view arg) {
        for (const ModeInfo &modeInfo : kModes) {
            if (arg == modeInfo.arg_) {
                return modeInfo;
            }
        }
        return kModes[0];
    }

    static bool captures(Mode mode) {
        return mode != Mode::NONE && mode != Mode::STANDARD_OFF;
    }

    static IRComponents::C_PostEffectsState state(Mode mode) {
        IRComponents::C_PostEffectsState state;
        state.blurRadius_ = 6.0f;
        state.bloomThreshold_ = 0.55f;
        state.bloomKnee_ = 0.15f;
        state.bloomIntensity_ = 1.5f;
        state.exposureStops_ = 0.25f;
        state.lift_ = IRMath::vec3(0.02f, 0.0f, 0.06f);
        state.gamma_ = IRMath::vec3(1.1f, 1.0f, 0.9f);
        state.gain_ = IRMath::vec3(1.1f, 1.0f, 0.85f);
        state.saturation_ = 1.4f;
        state.contrast_ = 1.2f;
        switch (mode) {
        case Mode::BLUR:
            state.blurEnabled_ = true;
            break;
        case Mode::BLOOM:
            state.bloomEnabled_ = true;
            break;
        case Mode::GRADE:
            state.gradeEnabled_ = true;
            break;
        case Mode::ALL:
            state.blurEnabled_ = true;
            state.blurRadius_ = 2.0f;
            state.bloomEnabled_ = true;
            state.bloomBlend_ = IRComponents::BloomBlend::SCREEN;
            state.gradeEnabled_ = true;
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

    static IRPrefab::PostEffects::Image readBack(const IRRender::Texture2D &colour) {
        const IRMath::ivec2 size = IRMath::ivec2(colour.getSize());
        IRPrefab::PostEffects::Image image = IRPrefab::PostEffects::Image::ofSize(size);
        colour.getSubImage2D(
            0,
            0,
            size.x,
            size.y,
            IRRender::PixelDataFormat::RGBA,
            IRRender::PixelDataType::UNSIGNED_BYTE,
            image.rgba_.data()
        );
        return image;
    }

    // Texels of @p output whose colour differs from @p control, and the
    // colour channels that fell below or rose above it.
    struct Difference {
        int changed_ = 0;
        int darker_ = 0;
        int brighter_ = 0;
        int maxDelta_ = 0;
    };

    static Difference compare(
        const IRPrefab::PostEffects::Image &output, const IRPrefab::PostEffects::Image &control
    ) {
        Difference difference;
        for (std::size_t texel = 0; texel < output.rgba_.size(); texel += 4) {
            bool changed = false;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const int delta = static_cast<int>(output.rgba_[texel + channel]) -
                                  static_cast<int>(control.rgba_[texel + channel]);
                changed = changed || delta != 0;
                difference.darker_ += delta < 0 ? 1 : 0;
                difference.brighter_ += delta > 0 ? 1 : 0;
                difference.maxDelta_ = IRMath::max(difference.maxDelta_, IRMath::abs(delta));
            }
            difference.changed_ += changed ? 1 : 0;
        }
        return difference;
    }

    static void logProbe(const char *when, Mode mode, bool readBackImages) {
        const auto *system = IRPrefab::PostEffects::systemOrNull();
        const IRSystem::SystemId screenId = IRSystem::findSystem(IRSystem::FRAMEBUFFER_TO_SCREEN);
        if (system == nullptr || screenId == IRSystem::kNullSystemId) {
            return;
        }
        const auto *screen =
            IRSystem::getSystemParams<IRSystem::System<IRSystem::FRAMEBUFFER_TO_SCREEN>>(screenId);
        const IRComponents::C_PostEffectsState current = IRPrefab::PostEffects::stateOrDefault();
        const IRPrefab::PostEffects::FramePlan plan = IRPrefab::PostEffects::planFrame(&current);
        const bool published = IRPrefab::PostEffects::outputPublishedForCurrentRenderTick();
        IR_LOG_INFO(
            "[post-effects-probe] at={} blur={} blurTaps={} bloom={} bloomLevels={} grade={} "
            "published={} frameDraws={} totalDraws={} frames={} ordinaryBlits={} "
            "postEffectsOutputBlits={} gpuMs={:.4f}",
            when,
            plan.blur_,
            plan.blurTaps_,
            plan.bloom_,
            plan.bloomLevels_,
            plan.grade_,
            published,
            system->frameDraws_,
            system->drawCount_,
            system->frameCount_,
            screen->ordinaryBlitCount_,
            screen->postEffectsOutputBlitCount_,
            IRRender::gpuStageTiming().framebufferPostEffectsMs_
        );
        if (!readBackImages || !published) {
            return;
        }
        // The readbacks need this frame's draws complete.
        IRRender::device()->finish();
        const IRPrefab::PostEffects::Image composite = readBack(
            IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer")
                .getTextureColor()
        );
        const IRPrefab::PostEffects::Image output = readBack(*system->outputColorOrNull());
        const Difference fromComposite = compare(output, composite);
        IR_LOG_INFO(
            "[post-effects-probe] output size={}x{} texels={} changed={} darker={} brighter={}",
            output.size_.x,
            output.size_.y,
            output.size_.x * output.size_.y,
            fromComposite.changed_,
            fromComposite.darker_,
            fromComposite.brighter_
        );
        if (mode != Mode::BLUR && mode != Mode::GRADE) {
            return;
        }
        IRPrefab::PostEffects::Image twin;
        if (mode == Mode::BLUR) {
            const auto weights = IRPrefab::PostEffects::blurWeights(current.blurRadius_);
            twin = IRPrefab::PostEffects::blurAxis(
                IRPrefab::PostEffects::blurAxis(
                    composite,
                    IRMath::ivec2(1, 0),
                    weights,
                    plan.blurTaps_
                ),
                IRMath::ivec2(0, 1),
                weights,
                plan.blurTaps_
            );
        } else {
            twin = IRPrefab::PostEffects::gradeImage(
                composite,
                IRPrefab::PostEffects::gradeParams(current)
            );
        }
        const Difference fromTwin = compare(output, twin);
        IR_LOG_INFO(
            "[post-effects-probe] cpuTwin mismatchedTexels={} cpuMaxDelta={} pass={}",
            fromTwin.changed_,
            fromTwin.maxDelta_,
            fromTwin.maxDelta_ <= 1
        );
    }
};

} // namespace IRShapeDebug

#endif /* POST_EFFECTS_FIXTURE_H */
