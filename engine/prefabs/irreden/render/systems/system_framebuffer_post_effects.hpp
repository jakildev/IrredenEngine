#ifndef SYSTEM_FRAMEBUFFER_POST_EFFECTS_H
#define SYSTEM_FRAMEBUFFER_POST_EFFECTS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_post_effects_state.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/framebuffer.hpp>
#include <irreden/render/framebuffer_screen_math.hpp>
#include <irreden/render/glitch.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_substage_timing.hpp>
#include <irreden/render/post_effects_math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace IRSystem {

// std140 twin of `FrameDataPostEffects` in `ir_post_effects_common.glsl` and
// `ir_post_effects_common.metal`.
struct FrameDataPostEffects {
    IRMath::mat4 mvpMatrix_ = IRMath::mat4(1.0f);
    // Bloom threshold, knee, intensity, scatter.
    IRMath::vec4 bloomParams_ = IRMath::vec4(0.0f);
    // Exposure multiplier, saturation, contrast.
    IRMath::vec4 gradeScalars_ = IRMath::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    IRMath::vec4 gradeLift_ = IRMath::vec4(0.0f);
    IRMath::vec4 gradeInverseGamma_ = IRMath::vec4(1.0f);
    IRMath::vec4 gradeGain_ = IRMath::vec4(1.0f);
    // Blur taps on one side of the centre, bloom on, `BloomBlend`, grade on.
    IRMath::ivec4 switches_ = IRMath::ivec4(0);
    // Blur weight for offset `i` is `blurWeights_[i / 4][i % 4]`.
    std::array<IRMath::vec4, IRPrefab::PostEffects::kBlurWeightVectors> blurWeights_{};
};
static_assert(offsetof(FrameDataPostEffects, bloomParams_) == 64);
static_assert(offsetof(FrameDataPostEffects, gradeScalars_) == 80);
static_assert(offsetof(FrameDataPostEffects, gradeLift_) == 96);
static_assert(offsetof(FrameDataPostEffects, gradeInverseGamma_) == 112);
static_assert(offsetof(FrameDataPostEffects, gradeGain_) == 128);
static_assert(offsetof(FrameDataPostEffects, switches_) == 144);
static_assert(offsetof(FrameDataPostEffects, blurWeights_) == 160);
static_assert(sizeof(FrameDataPostEffects) == 304);

inline FrameDataPostEffects packPostEffectsFrameData(
    const IRComponents::C_PostEffectsState &state,
    const IRPrefab::PostEffects::FramePlan &plan,
    const IRMath::mat4 &mvpMatrix
) {
    FrameDataPostEffects data;
    data.mvpMatrix_ = mvpMatrix;
    data.bloomParams_ = IRMath::vec4(
        state.bloomThreshold_,
        state.bloomKnee_,
        state.bloomIntensity_,
        state.bloomScatter_
    );
    const IRPrefab::PostEffects::GradeParams grade = IRPrefab::PostEffects::gradeParams(state);
    data.gradeScalars_ = IRMath::vec4(grade.exposure_, grade.saturation_, grade.contrast_, 0.0f);
    data.gradeLift_ = IRMath::vec4(grade.lift_, 0.0f);
    data.gradeInverseGamma_ = IRMath::vec4(grade.inverseGamma_, 1.0f);
    data.gradeGain_ = IRMath::vec4(grade.gain_, 1.0f);
    data.switches_ = IRMath::ivec4(
        plan.blurTaps_,
        plan.bloom_ ? 1 : 0,
        static_cast<int>(state.bloomBlend_),
        plan.grade_ ? 1 : 0
    );
    const auto weights = IRPrefab::PostEffects::blurWeights(state.blurRadius_);
    for (std::size_t offset = 0; offset < weights.size(); ++offset) {
        data.blurWeights_[offset / 4][static_cast<int>(offset % 4)] = weights[offset];
    }
    return data;
}

// One draw of an enabled frame. A bloom pass also names the level it writes.
enum class PostEffectsPass {
    BLUR_HORIZONTAL,
    BLUR_VERTICAL,
    BLOOM_PREFILTER,
    BLOOM_DOWNSAMPLE,
    BLOOM_UPSAMPLE,
    RESOLVE,
};

// The GPU work of one enabled frame. `encodeFrame` issues it in this order
// and nothing else: `destroyImages` on a resize; `createOutput`,
// `createBlurImages` and `createBloomImages` for images the frame needs and
// the pass does not hold; one `uploadFrameData`; then one `drawPass` per
// stage, ending with `RESOLVE`. `PostEffectsGpuEncoder` encodes it; the
// headless probes record it.
class PostEffectsEncoder {
  public:
    virtual void destroyImages() = 0;
    virtual void createOutput(IRMath::ivec2 size) = 0;
    virtual void createBlurImages(IRMath::ivec2 size) = 0;
    virtual void createBloomImages(IRMath::ivec2 size) = 0;
    virtual void uploadFrameData(const FrameDataPostEffects &frameData) = 0;
    virtual void
    drawPass(PostEffectsPass pass, int level, const IRPrefab::PostEffects::FramePlan &plan) = 0;

  protected:
    ~PostEffectsEncoder() = default;
};

// Every image the pass owns; a null entry is not allocated.
struct PostEffectsImages {
    using Image = std::pair<ResourceId, Framebuffer *>;

    // The presented result, the main framebuffer's twin texel for texel.
    Image output_{0, nullptr};
    // Horizontal pass result, then the finished blur.
    std::array<Image, 2> blur_{};
    std::array<Image, IRPrefab::PostEffects::kMaxBloomLevels> bloomDown_{};
    // `bloomUp_[i]` is level `i` with every coarser level folded in. The
    // coarsest level has no entry: its down image is its up image.
    std::array<Image, IRPrefab::PostEffects::kMaxBloomLevels - 1> bloomUp_{};
};

struct PostEffectsPrograms {
    ShaderProgram *blurHorizontal_ = nullptr;
    ShaderProgram *blurVertical_ = nullptr;
    ShaderProgram *bloomPrefilter_ = nullptr;
    ShaderProgram *bloomDownsample_ = nullptr;
    ShaderProgram *bloomUpsample_ = nullptr;
    ShaderProgram *resolve_ = nullptr;
};

class PostEffectsGpuEncoder final : public PostEffectsEncoder {
  public:
    // @p source is the picture the frame starts from: the image presented for
    // the main framebuffer before this pass.
    PostEffectsGpuEncoder(
        const Texture2D &source,
        PostEffectsImages &images,
        Buffer &frameDataBuf,
        const PostEffectsPrograms &programs,
        VAO &quadVao
    )
        : m_source{source}
        , m_images{images}
        , m_frameDataBuf{frameDataBuf}
        , m_programs{programs}
        , m_quadVao{quadVao} {}

    void destroyImages() override {
        destroy(m_images.output_);
        for (auto &image : m_images.blur_) {
            destroy(image);
        }
        for (auto &image : m_images.bloomDown_) {
            destroy(image);
        }
        for (auto &image : m_images.bloomUp_) {
            destroy(image);
        }
    }

    void createOutput(IRMath::ivec2 size) override {
        m_images.output_ = create(size);
    }

    void createBlurImages(IRMath::ivec2 size) override {
        for (auto &image : m_images.blur_) {
            image = create(size);
        }
    }

    void createBloomImages(IRMath::ivec2 size) override {
        for (int level = 0; level < IRPrefab::PostEffects::kMaxBloomLevels; ++level) {
            const IRMath::ivec2 levelSize = IRPrefab::PostEffects::bloomLevelSize(size, level);
            m_images.bloomDown_[static_cast<std::size_t>(level)] = create(levelSize);
            if (level + 1 < IRPrefab::PostEffects::kMaxBloomLevels) {
                m_images.bloomUp_[static_cast<std::size_t>(level)] = create(levelSize);
            }
        }
    }

    void uploadFrameData(const FrameDataPostEffects &frameData) override {
        m_frameDataBuf.subData(0, sizeof(FrameDataPostEffects), &frameData);
        m_frameDataBuf.bindBase(
            BufferTarget::UNIFORM,
            kBufferIndex_FramebufferPostEffectsFrameData
        );
    }

    void drawPass(
        PostEffectsPass pass, int level, const IRPrefab::PostEffects::FramePlan &plan
    ) override {
        const auto index = static_cast<std::size_t>(level);
        const Texture2D &base = plan.blur_ ? color(m_images.blur_[1]) : m_source;
        switch (pass) {
        case PostEffectsPass::BLUR_HORIZONTAL:
            draw(m_images.blur_[0], *m_programs.blurHorizontal_, m_source, m_source);
            return;
        case PostEffectsPass::BLUR_VERTICAL:
            draw(
                m_images.blur_[1],
                *m_programs.blurVertical_,
                color(m_images.blur_[0]),
                color(m_images.blur_[0])
            );
            return;
        case PostEffectsPass::BLOOM_PREFILTER:
            draw(m_images.bloomDown_[0], *m_programs.bloomPrefilter_, base, base);
            return;
        case PostEffectsPass::BLOOM_DOWNSAMPLE:
            draw(
                m_images.bloomDown_[index],
                *m_programs.bloomDownsample_,
                color(m_images.bloomDown_[index - 1]),
                color(m_images.bloomDown_[index - 1])
            );
            return;
        case PostEffectsPass::BLOOM_UPSAMPLE:
            draw(
                m_images.bloomUp_[index],
                *m_programs.bloomUpsample_,
                color(m_images.bloomDown_[index]),
                foldedLevel(level + 1, plan)
            );
            return;
        case PostEffectsPass::RESOLVE:
            // The resolve declares the glow at unit 1 whether or not bloom is
            // on, so an off bloom binds the picture there.
            draw(
                m_images.output_,
                *m_programs.resolve_,
                base,
                plan.bloom_ ? foldedLevel(0, plan) : base
            );
            return;
        }
    }

  private:
    static const Texture2D &color(const PostEffectsImages::Image &image) {
        return image.second->getTextureColor();
    }

    // Level @p level with every coarser level folded in.
    const Texture2D &foldedLevel(int level, const IRPrefab::PostEffects::FramePlan &plan) const {
        const auto index = static_cast<std::size_t>(level);
        return level == plan.bloomLevels_ - 1 ? color(m_images.bloomDown_[index])
                                              : color(m_images.bloomUp_[index]);
    }

    // RGBA8 like the composite; the depth attachment only completes the
    // target.
    static PostEffectsImages::Image create(IRMath::ivec2 size) {
        return IRRender::createResource<Framebuffer>(
            size,
            IRMath::ivec2(0),
            TextureFormat::RGBA8,
            TextureFormat::DEPTH24_STENCIL8
        );
    }

    static void destroy(PostEffectsImages::Image &image) {
        if (image.second != nullptr) {
            IRRender::destroyResource<Framebuffer>(image.first);
        }
        image = {0, nullptr};
    }

    void draw(
        const PostEffectsImages::Image &target,
        ShaderProgram &program,
        const Texture2D &first,
        const Texture2D &second
    ) {
        target.second->bind();
        target.second->clear();
        program.use();
        m_quadVao.bind();
        first.bind(0);
        second.bind(1);
        IRRender::device()->setPolygonMode(PolygonMode::FILL);
        IRRender::device()->drawArrays(DrawMode::TRIANGLES, 0, 6);
    }

    const Texture2D &m_source;
    PostEffectsImages &m_images;
    Buffer &m_frameDataBuf;
    const PostEffectsPrograms &m_programs;
    VAO &m_quadVao;
};

// Bloom, blur and a colour grade over the image presented for the main
// framebuffer, drawn into an output image of the same size on enabled
// frames. `FRAMEBUFFER_TO_SCREEN` then presents the output and the recorder
// reads it (`IRPrefab::PresentedFrame::mainColor`). Off frames encode no GPU
// work. Contract: docs/design/post-effects.md.
template <> struct System<FRAMEBUFFER_POST_EFFECTS> {
    static constexpr std::uint64_t kNotPublished = std::numeric_limits<std::uint64_t>::max();

    Buffer *frameDataBuf_ = nullptr;
    PostEffectsPrograms programs_{};
    VAO *quadVao_ = nullptr;
    FrameDataPostEffects frameData_{};

    // Sized from the main framebuffer's resolution-plus-buffer and replaced
    // with it; the rendering resource manager owns their lifetime.
    PostEffectsImages images_{};
    IRMath::ivec2 imageSize_ = IRMath::ivec2(0);
    bool hasOutput_ = false;
    bool hasBlurImages_ = false;
    bool hasBloomImages_ = false;

    // Frame state resolved in beginTick.
    IRComponents::C_PostEffectsState state_{};
    IRPrefab::PostEffects::FramePlan plan_{};
    std::uint64_t renderTick_ = 0;

    // RENDER event tick whose output this pass drew, stamped after the
    // resolve is encoded.
    std::uint64_t publishedRenderTick_ = kNotPublished;

    // Encoder draws issued, this frame and since registration; the
    // `[post-effects-probe]` lines report them.
    int frameDraws_ = 0;
    std::uint64_t drawCount_ = 0;
    std::uint64_t frameCount_ = 0;

    bool isPublishedForRenderTick(std::uint64_t renderTick) const {
        return publishedRenderTick_ == renderTick;
    }

    // The output's colour, or null before the first enabled frame. Current
    // only on a RENDER tick `isPublishedForRenderTick` reports.
    const Texture2D *outputColorOrNull() const {
        return images_.output_.second != nullptr ? &images_.output_.second->getTextureColor()
                                                 : nullptr;
    }

    void beginTick() {
        renderTick_ = IRSystem::getEventTickCount(IRTime::Events::RENDER);
        frameDraws_ = 0;
        const IRComponents::C_PostEffectsState *state =
            IREntity::singletonOrNull<IRComponents::C_PostEffectsState>();
        plan_ = IRPrefab::PostEffects::planFrame(state);
        if (!plan_.enabled()) {
            // Off: no scope, no upload, no draw. The live row reads zero
            // rather than the last enabled frame's sample.
            IRRender::gpuStageTiming().framebufferPostEffectsMs_ = 0.0f;
            return;
        }
        state_ = *state;
    }

    // One enabled frame through @p encoder. A resize withdraws the
    // publication before the images are replaced, and the pass publishes only
    // after the resolve, so no reader presents an output this pass did not
    // draw.
    void encodeFrame(PostEffectsEncoder &encoder, IRMath::ivec2 framebufferSize) {
        if (imageSize_ != framebufferSize) {
            publishedRenderTick_ = kNotPublished;
            if (hasOutput_) {
                encoder.destroyImages();
            }
            hasOutput_ = false;
            hasBlurImages_ = false;
            hasBloomImages_ = false;
            imageSize_ = framebufferSize;
        }
        if (!hasOutput_) {
            encoder.createOutput(framebufferSize);
            hasOutput_ = true;
        }
        if (plan_.blur_ && !hasBlurImages_) {
            encoder.createBlurImages(framebufferSize);
            hasBlurImages_ = true;
        }
        if (plan_.bloom_ && !hasBloomImages_) {
            encoder.createBloomImages(framebufferSize);
            hasBloomImages_ = true;
        }

        frameData_ =
            packPostEffectsFrameData(state_, plan_, detail::framebufferSelfMvp(framebufferSize));
        {
            IRRender::GpuSubStageScope scope("framebufferPostEffects");
            encoder.uploadFrameData(frameData_);
            if (plan_.blur_) {
                draw(encoder, PostEffectsPass::BLUR_HORIZONTAL, 0);
                draw(encoder, PostEffectsPass::BLUR_VERTICAL, 0);
            }
            if (plan_.bloom_) {
                draw(encoder, PostEffectsPass::BLOOM_PREFILTER, 0);
                for (int level = 1; level < plan_.bloomLevels_; ++level) {
                    draw(encoder, PostEffectsPass::BLOOM_DOWNSAMPLE, level);
                }
                for (int level = plan_.bloomLevels_ - 2; level >= 0; --level) {
                    draw(encoder, PostEffectsPass::BLOOM_UPSAMPLE, level);
                }
            }
            draw(encoder, PostEffectsPass::RESOLVE, 0);
        }
        ++frameCount_;
        publishedRenderTick_ = renderTick_;
    }

    void tick(const C_TrixelCanvasFramebuffer &framebuffer, const C_Name &name) {
        if (!plan_.enabled() || name.name_ != "mainFramebuffer") {
            return;
        }
        IR_PROFILE_SCOPE("framebufferPostEffects");
        const Texture2D *glitched = IRPrefab::Glitch::presentedMainColorOrNull();
        PostEffectsGpuEncoder encoder{
            glitched != nullptr ? *glitched : framebuffer.getTextureColor(),
            images_,
            *frameDataBuf_,
            programs_,
            *quadVao_
        };
        encodeFrame(encoder, framebuffer.getResolutionPlusBuffer());
    }

    static SystemId create() {
        const auto program = [](const char *name, const char *fragment) {
            IRRender::createNamedResource<ShaderProgram>(
                name,
                std::vector{
                    ShaderStage{IRRender::kFileVertPostEffects, ShaderType::VERTEX},
                    ShaderStage{fragment, ShaderType::FRAGMENT}
                }
            );
            return IRRender::getNamedResource<ShaderProgram>(name);
        };
        IRRender::createNamedResource<Buffer>(
            "FramebufferPostEffectsFrameData",
            nullptr,
            sizeof(FrameDataPostEffects),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FramebufferPostEffectsFrameData
        );
        SystemId id = registerSystem<FRAMEBUFFER_POST_EFFECTS, C_TrixelCanvasFramebuffer, C_Name>(
            "FramebufferPostEffects"
        );
        auto *sys = getSystemParams<System<FRAMEBUFFER_POST_EFFECTS>>(id);
        sys->frameDataBuf_ = IRRender::getNamedResource<Buffer>("FramebufferPostEffectsFrameData");
        sys->programs_.blurHorizontal_ = program(
            "PostEffectsBlurHorizontalProgram",
            IRRender::kFileFragPostEffectsBlurHorizontal
        );
        sys->programs_.blurVertical_ =
            program("PostEffectsBlurVerticalProgram", IRRender::kFileFragPostEffectsBlurVertical);
        sys->programs_.bloomPrefilter_ = program(
            "PostEffectsBloomPrefilterProgram",
            IRRender::kFileFragPostEffectsBloomPrefilter
        );
        sys->programs_.bloomDownsample_ = program(
            "PostEffectsBloomDownsampleProgram",
            IRRender::kFileFragPostEffectsBloomDownsample
        );
        sys->programs_.bloomUpsample_ =
            program("PostEffectsBloomUpsampleProgram", IRRender::kFileFragPostEffectsBloomUpsample);
        sys->programs_.resolve_ =
            program("PostEffectsResolveProgram", IRRender::kFileFragPostEffects);
        sys->quadVao_ = IRRender::getNamedResource<VAO>("QuadVAOArrays");
        // Deliberately untagged: `encodeFrame`'s GpuSubStageScope owns the
        // `framebufferPostEffects` row, and a system tag would share its
        // timestamp attachment.
        return id;
    }

    void draw(PostEffectsEncoder &encoder, PostEffectsPass pass, int level) {
        encoder.drawPass(pass, level, plan_);
        ++frameDraws_;
        ++drawCount_;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FRAMEBUFFER_POST_EFFECTS_H */
