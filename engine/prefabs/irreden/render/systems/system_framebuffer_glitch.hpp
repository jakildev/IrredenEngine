#ifndef SYSTEM_FRAMEBUFFER_GLITCH_H
#define SYSTEM_FRAMEBUFFER_GLITCH_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_glitch_state.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/framebuffer.hpp>
#include <irreden/render/framebuffer_screen_math.hpp>
#include <irreden/render/glitch_math.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_substage_timing.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace IRSystem {

// std140 twin of `FrameDataFramebufferGlitch` in `v_framebuffer_glitch.glsl`,
// `f_framebuffer_glitch.glsl` and `framebuffer_glitch.metal`.
struct FrameDataFramebufferGlitch {
    IRMath::mat4 mvpMatrix_ = IRMath::mat4(1.0f);
    // Period tick low/high words, then hold tick low/high words.
    IRMath::uvec4 tickWords_ = IRMath::uvec4(0u);
    // seed, cell threshold, depth probe start, depth enabled.
    IRMath::uvec4 hashInputs_ = IRMath::uvec4(0u);
    // Framebuffer pixels per trixel (xy) and per cell (zw).
    IRMath::ivec4 trixelStep_ = IRMath::ivec4(1);
    // Band counts: tear, hold, cells; then the bar count.
    IRMath::ivec4 counts_ = IRMath::ivec4(0);
    // Split and depth-tear x offsets in framebuffer pixels.
    IRMath::ivec4 offsets_ = IRMath::ivec4(0);
    // Depth-hold blend, depth half-width (normalized depth), bar strength.
    IRMath::vec4 strengths_ = IRMath::vec4(0.0f);
    // Rows [x, y) of each band; tear bands carry their x shift in z.
    std::array<IRMath::ivec4, IRPrefab::Glitch::kMaxBands> tearBands_{};
    std::array<IRMath::ivec4, IRPrefab::Glitch::kMaxBands> holdBands_{};
    std::array<IRMath::ivec4, IRPrefab::Glitch::kMaxBands> cellBands_{};
    IRMath::ivec4 barRows_ = IRMath::ivec4(-1);
    IRMath::ivec4 barColours_ = IRMath::ivec4(0);
};
static_assert(offsetof(FrameDataFramebufferGlitch, tickWords_) == 64);
static_assert(offsetof(FrameDataFramebufferGlitch, hashInputs_) == 80);
static_assert(offsetof(FrameDataFramebufferGlitch, trixelStep_) == 96);
static_assert(offsetof(FrameDataFramebufferGlitch, counts_) == 112);
static_assert(offsetof(FrameDataFramebufferGlitch, offsets_) == 128);
static_assert(offsetof(FrameDataFramebufferGlitch, strengths_) == 144);
static_assert(offsetof(FrameDataFramebufferGlitch, tearBands_) == 160);
static_assert(offsetof(FrameDataFramebufferGlitch, holdBands_) == 256);
static_assert(offsetof(FrameDataFramebufferGlitch, cellBands_) == 352);
static_assert(offsetof(FrameDataFramebufferGlitch, barRows_) == 448);
static_assert(offsetof(FrameDataFramebufferGlitch, barColours_) == 464);
static_assert(sizeof(FrameDataFramebufferGlitch) == 480);
static_assert(
    IRPrefab::Glitch::kMaxBands == 6 && IRPrefab::Glitch::kMaxBars <= 4,
    "the shader blocks declare six bands per effect and one ivec4 of bars"
);

inline FrameDataFramebufferGlitch packGlitchFrameData(
    const IRPrefab::Glitch::FrameTables &tables,
    IRPrefab::Glitch::TickWords holdTick,
    const IRMath::mat4 &mvpMatrix
) {
    FrameDataFramebufferGlitch data;
    data.mvpMatrix_ = mvpMatrix;
    data.tickWords_ =
        IRMath::uvec4(tables.tick_.low_, tables.tick_.high_, holdTick.low_, holdTick.high_);
    data.hashInputs_ = IRMath::uvec4(
        tables.seed_,
        tables.cellThreshold_,
        static_cast<std::uint32_t>(tables.depthProbeStart_),
        tables.depthEnabled_ ? 1u : 0u
    );
    data.trixelStep_ = IRMath::ivec4(tables.trixelStep_, tables.cellPx_);
    data.counts_ =
        IRMath::ivec4(tables.tearCount_, tables.holdCount_, tables.cellCount_, tables.barCount_);
    data.offsets_ = IRMath::ivec4(tables.splitOffsetPx_, tables.depthTearPx_, 0, 0);
    data.strengths_ =
        IRMath::vec4(tables.depthHoldBlend_, tables.depthHalfWidth_, tables.barStrength_, 0.0f);
    for (int band = 0; band < IRPrefab::Glitch::kMaxBands; ++band) {
        const auto &tear = tables.tear_[band];
        data.tearBands_[band] = IRMath::ivec4(tear.start_, tear.end_, tear.offsetPx_, 0);
        data.holdBands_[band] =
            IRMath::ivec4(tables.hold_[band].start_, tables.hold_[band].end_, 0, 0);
        data.cellBands_[band] =
            IRMath::ivec4(tables.cells_[band].start_, tables.cells_[band].end_, 0, 0);
    }
    for (int bar = 0; bar < tables.barCount_; ++bar) {
        data.barRows_[bar] = tables.barRows_[bar];
        data.barColours_[bar] = tables.barColours_[bar];
    }
    return data;
}

// The decision one RENDER tick makes before touching the GPU. Pure so the
// off/enabled and history-cadence contract is testable headless.
struct GlitchFramePlan {
    bool enabled_ = false;
    bool copyHistory_ = false;
};

inline GlitchFramePlan planGlitchFrame(
    const IRComponents::C_GlitchState *state,
    std::int64_t holdTick,
    bool historyValid,
    std::int64_t lastHoldTick
) {
    if (state == nullptr || !IRPrefab::Glitch::isEnabled(*state)) {
        return {};
    }
    // Only hold and depth hold read the history; without them it stays bound
    // but is never refreshed.
    const bool readsHistory = state->holdWeight_ > 0.0f || state->depthWeight_ > 0.0f;
    return {true, readsHistory && (!historyValid || holdTick != lastHoldTick)};
}

// The GPU work of one enabled frame. `encodeFrame` issues it in this order and
// nothing else: `recreateImages` on first use and on a resize, `copyHistory`
// on a hold-tick change, then always one `bindAndClearOutput` and one
// `drawOutput`. `GlitchGpuEncoder` encodes it; the headless probes record it.
class GlitchEncoder {
  public:
    // Replaces the history image and the output framebuffer with ones of
    // @p size.
    virtual void recreateImages(IRMath::ivec2 size) = 0;
    // Snapshots the unglitched main colour into the history image.
    virtual void copyHistory() = 0;
    virtual void bindAndClearOutput() = 0;
    virtual void drawOutput(const FrameDataFramebufferGlitch &frameData) = 0;

  protected:
    ~GlitchEncoder() = default;
};

class GlitchGpuEncoder final : public GlitchEncoder {
  public:
    GlitchGpuEncoder(
        const C_TrixelCanvasFramebuffer &source,
        std::pair<ResourceId, Texture2D *> &history,
        std::pair<ResourceId, Framebuffer *> &output,
        Buffer &frameDataBuf,
        ShaderProgram &program,
        VAO &quadVao
    )
        : m_source{source}
        , m_history{history}
        , m_output{output}
        , m_frameDataBuf{frameDataBuf}
        , m_program{program}
        , m_quadVao{quadVao} {}

    void recreateImages(IRMath::ivec2 size) override {
        if (m_history.second != nullptr) {
            IRRender::destroyResource<Texture2D>(m_history.first);
        }
        if (m_output.second != nullptr) {
            IRRender::destroyResource<Framebuffer>(m_output.first);
        }
        m_history = IRRender::createResource<Texture2D>(
            TextureKind::TEXTURE_2D,
            static_cast<unsigned int>(size.x),
            static_cast<unsigned int>(size.y),
            TextureFormat::RGBA8,
            TextureWrap::CLAMP_TO_EDGE,
            TextureFilter::NEAREST
        );
        // The output is the main colour's twin texel for texel, so the blit
        // and the recorder read it exactly as they read the composite. Its
        // depth attachment only completes the target.
        m_output = IRRender::createResource<Framebuffer>(
            size,
            IRMath::ivec2(0),
            TextureFormat::RGBA8,
            TextureFormat::DEPTH24_STENCIL8
        );
    }

    void copyHistory() override {
        IRRender::device()->copyTexture2D(m_source.getTextureColor(), *m_history.second);
    }

    void bindAndClearOutput() override {
        m_output.second->bind();
        m_output.second->clear();
    }

    void drawOutput(const FrameDataFramebufferGlitch &frameData) override {
        m_frameDataBuf.subData(0, sizeof(FrameDataFramebufferGlitch), &frameData);
        m_frameDataBuf.bindBase(BufferTarget::UNIFORM, kBufferIndex_FramebufferGlitchFrameData);
        m_program.use();
        m_quadVao.bind();
        m_source.bindTextures(0, 1);
        m_history.second->bind(2);
        IRRender::device()->setPolygonMode(PolygonMode::FILL);
        IRRender::device()->drawArrays(DrawMode::TRIANGLES, 0, 6);
    }

  private:
    const C_TrixelCanvasFramebuffer &m_source;
    std::pair<ResourceId, Texture2D *> &m_history;
    std::pair<ResourceId, Framebuffer *> &m_output;
    Buffer &m_frameDataBuf;
    ShaderProgram &m_program;
    VAO &m_quadVao;
};

// Glitches the main framebuffer's composite into an output image of the same
// size on enabled frames: one fullscreen fragment reads the composed main
// colour, its depth attachment and a history snapshot. `FRAMEBUFFER_TO_SCREEN`
// then presents the output in place of the composite, and the recorder reads
// the same output, so a clip carries what the screen showed
// (`IRPrefab::Glitch::presentedMainColorOrNull`). Owns two images, the history
// texture and the output framebuffer. Off frames encode no GPU work.
template <> struct System<FRAMEBUFFER_GLITCH> {
    static constexpr std::uint64_t kNotPublished = std::numeric_limits<std::uint64_t>::max();

    Buffer *frameDataBuf_ = nullptr;
    ShaderProgram *program_ = nullptr;
    VAO *quadVao_ = nullptr;
    FrameDataFramebufferGlitch frameData_{};

    // Unglitched snapshot of the composed main colour, retaken on each hold
    // tick, and the glitched output. Both are sized to the main framebuffer's
    // resolution-plus-buffer and recreated with it; the rendering resource
    // manager owns their lifetime.
    std::pair<ResourceId, Texture2D *> history_{0, nullptr};
    std::pair<ResourceId, Framebuffer *> output_{0, nullptr};
    IRMath::ivec2 imageSize_ = IRMath::ivec2(0);
    bool historyValid_ = false;
    std::int64_t lastHoldTick_ = 0;

    // Frame state resolved in beginTick.
    IRComponents::C_GlitchState state_{};
    IRPrefab::Glitch::Clock clock_{};
    bool enabledThisFrame_ = false;
    std::uint64_t renderTick_ = 0;

    // RENDER event tick whose output this pass drew, stamped after the draw
    // is encoded.
    std::uint64_t publishedRenderTick_ = kNotPublished;

    // Encoder calls issued, per frame and since registration; the
    // `[glitch-probe]` lines report them.
    int frameHistoryCopies_ = 0;
    int frameDraws_ = 0;
    std::uint64_t historyCopyCount_ = 0;
    std::uint64_t drawCount_ = 0;

    bool isPublishedForRenderTick(std::uint64_t renderTick) const {
        return publishedRenderTick_ == renderTick;
    }

    bool ownsImages() const {
        return history_.second != nullptr && output_.second != nullptr;
    }

    // The glitched output's colour, or null before the first enabled frame.
    // Current only on a RENDER tick `isPublishedForRenderTick` reports.
    const Texture2D *outputColorOrNull() const {
        return output_.second != nullptr ? &output_.second->getTextureColor() : nullptr;
    }

    void beginTick() {
        renderTick_ = IRSystem::getEventTickCount(IRTime::Events::RENDER);
        frameHistoryCopies_ = 0;
        frameDraws_ = 0;
        const IRComponents::C_GlitchState *state =
            IREntity::singletonOrNull<IRComponents::C_GlitchState>();
        enabledThisFrame_ = state != nullptr && IRPrefab::Glitch::isEnabled(*state);
        if (!enabledThisFrame_) {
            // Off: no scope, no copy, no draw. The live row reads zero rather
            // than the last enabled frame's sample, and history is reseeded
            // on the next enabled frame.
            historyValid_ = false;
            IRRender::gpuStageTiming().framebufferGlitchMs_ = 0.0f;
            return;
        }
        state_ = *state;
        clock_ = IRPrefab::Glitch::clockAt(state_, IRPrefab::Glitch::engineTimeSeconds());
    }

    // One enabled frame through @p encoder. A resize withdraws the publication
    // before the images are replaced: the snapshot and any output drawn
    // against the old size are void. The pass publishes only after the output
    // draw, so no reader presents an output this pass did not draw.
    void
    encodeFrame(GlitchEncoder &encoder, IRMath::ivec2 framebufferSize, IRMath::ivec2 trixelStep) {
        if (imageSize_ != framebufferSize) {
            publishedRenderTick_ = kNotPublished;
            historyValid_ = false;
            imageSize_ = framebufferSize;
            encoder.recreateImages(framebufferSize);
        }
        const bool copyHistory =
            planGlitchFrame(&state_, clock_.holdTick_, historyValid_, lastHoldTick_).copyHistory_;

        const IRPrefab::Glitch::FrameTables tables = IRPrefab::Glitch::frameTables(
            IRPrefab::Glitch::frameParams(state_, clock_, trixelStep, framebufferSize)
        );
        frameData_ = packGlitchFrameData(
            tables,
            IRPrefab::Glitch::toTickWords(clock_.holdTick_),
            detail::framebufferSelfMvp(framebufferSize)
        );

        {
            IRRender::GpuSubStageScope scope("framebufferGlitch");
            if (copyHistory) {
                // Snapshot from the unglitched composite: feeding glitched
                // output back would accumulate artifacts.
                encoder.copyHistory();
                historyValid_ = true;
                lastHoldTick_ = clock_.holdTick_;
                ++frameHistoryCopies_;
                ++historyCopyCount_;
            }
            encoder.bindAndClearOutput();
            encoder.drawOutput(frameData_);
        }
        ++frameDraws_;
        ++drawCount_;
        publishedRenderTick_ = renderTick_;
    }

    void tick(const C_TrixelCanvasFramebuffer &framebuffer, const C_Name &name) {
        if (!enabledThisFrame_ || name.name_ != "mainFramebuffer") {
            return;
        }
        IR_PROFILE_SCOPE("framebufferGlitch");
        GlitchGpuEncoder
            encoder{framebuffer, history_, output_, *frameDataBuf_, *program_, *quadVao_};
        encodeFrame(
            encoder,
            framebuffer.getResolutionPlusBuffer(),
            IRPrefab::Glitch::trixelStepForZoom(IRRender::getCameraZoom())
        );
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "FramebufferGlitchProgram",
            std::vector{
                ShaderStage{IRRender::kFileVertFramebufferGlitch, ShaderType::VERTEX},
                ShaderStage{IRRender::kFileFragFramebufferGlitch, ShaderType::FRAGMENT}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "FramebufferGlitchFrameData",
            nullptr,
            sizeof(FrameDataFramebufferGlitch),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FramebufferGlitchFrameData
        );
        SystemId id = registerSystem<FRAMEBUFFER_GLITCH, C_TrixelCanvasFramebuffer, C_Name>(
            "FramebufferGlitch"
        );
        auto *sys = getSystemParams<System<FRAMEBUFFER_GLITCH>>(id);
        sys->frameDataBuf_ = IRRender::getNamedResource<Buffer>("FramebufferGlitchFrameData");
        sys->program_ = IRRender::getNamedResource<ShaderProgram>("FramebufferGlitchProgram");
        sys->quadVao_ = IRRender::getNamedResource<VAO>("QuadVAOArrays");
        // Deliberately untagged: `encodeFrame`'s GpuSubStageScope owns the
        // `framebufferGlitch` row, and a system tag would share its timestamp
        // attachment.
        return id;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FRAMEBUFFER_GLITCH_H */
