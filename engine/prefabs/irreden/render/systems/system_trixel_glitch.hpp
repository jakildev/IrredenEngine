#ifndef SYSTEM_TRIXEL_GLITCH_H
#define SYSTEM_TRIXEL_GLITCH_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_glitch_state.hpp>
#include <irreden/render/components/component_glitch_subject.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/glitch_math.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_substage_timing.hpp>
#include <irreden/render/trixel_glitch_math.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace IRSystem {

// The decision one RENDER tick makes before touching the GPU. Pure so the
// off/enabled, image and history-cadence contract is testable headless.
struct TrixelGlitchFramePlan {
    bool enabled_ = false;
    bool createImages_ = false;
    bool copyHistory_ = false;
};

// History is the colour and id planes as they stood at the start of the
// current period tick, so a hold subject freezes to its tick-start pose. It is
// retaken when a subject reads it and the snapshot is missing, stale, or from
// another tick.
inline TrixelGlitchFramePlan planTrixelGlitchFrame(
    bool anyActive,
    bool readsHistory,
    bool imagesAllocated,
    bool historyValid,
    std::int64_t tick,
    std::int64_t lastTick
) {
    if (!anyActive) {
        return {};
    }
    const bool createImages = !imagesAllocated;
    return {
        true,
        createImages,
        readsHistory && (createImages || !historyValid || tick != lastTick)
    };
}

// The GPU work of one enabled frame. `encodeFrame` issues it in this order:
// `createImages` on first use and after a resize, `uploadSubjects`, the two
// history copies on a new tick, `dispatch`, then `swapColors`. Nothing ever
// writes the distance or entity-id planes. `TrixelGlitchGpuEncoder` encodes
// it; the headless probes record it.
class TrixelGlitchEncoder {
  public:
    // Gives the main canvas its three glitch images at its current size.
    virtual void createImages() = 0;
    virtual void uploadSubjects(std::span<const IRMath::uvec4> words) = 0;
    virtual void copyHistoryColors() = 0;
    virtual void copyHistoryEntityIds() = 0;
    // One full-canvas dispatch reading the live planes and history and
    // writing every scratch texel, then an image barrier.
    virtual void dispatch() = 0;
    virtual void swapColors() = 0;

  protected:
    ~TrixelGlitchEncoder() = default;
};

class TrixelGlitchGpuEncoder final : public TrixelGlitchEncoder {
  public:
    // First allocation; growth doubles from here.
    static constexpr std::size_t kMinSubjectBufferBytes = 4096;

    TrixelGlitchGpuEncoder(
        IRComponents::C_TriangleCanvasTextures &canvas,
        ShaderProgram &program,
        std::pair<ResourceId, Buffer *> &subjectBuf,
        std::size_t &subjectBufBytes,
        Buffer *slotOwner
    )
        : m_canvas{canvas}
        , m_program{program}
        , m_subjectBuf{subjectBuf}
        , m_subjectBufBytes{subjectBufBytes}
        , m_slotOwner{slotOwner} {}

    void createImages() override {
        m_canvas.createGlitchImages();
    }

    void uploadSubjects(std::span<const IRMath::uvec4> words) override {
        const std::size_t bytes = words.size_bytes();
        if (bytes > m_subjectBufBytes) {
            std::size_t capacity = kMinSubjectBufferBytes;
            while (capacity < bytes) {
                capacity *= 2;
            }
            if (m_subjectBuf.second != nullptr) {
                IRRender::destroyResource<Buffer>(m_subjectBuf.first);
            }
            m_subjectBuf = IRRender::createResource<Buffer>(
                nullptr,
                capacity,
                BUFFER_STORAGE_DYNAMIC,
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_TrixelGlitchSubjects
            );
            m_subjectBufBytes = capacity;
        }
        m_subjectBuf.second->subData(0, bytes, words.data());
    }

    void copyHistoryColors() override {
        IRRender::device()->copyTexture2D(
            *m_canvas.getTextureColors(),
            *m_canvas.glitchHistoryColors_.second
        );
    }

    void copyHistoryEntityIds() override {
        IRRender::device()->copyTexture2D(
            *m_canvas.getTextureEntityIds(),
            *m_canvas.glitchHistoryEntityIds_.second
        );
    }

    void dispatch() override {
        m_program.use();
        m_canvas.getTextureColors()->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        m_canvas.getTextureDistances()
            ->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
        m_canvas.getTextureEntityIds()
            ->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
        m_canvas.glitchHistoryColors_.second
            ->bindAsImage(3, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        m_canvas.glitchHistoryEntityIds_.second
            ->bindAsImage(4, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
        m_canvas.glitchScratchColors_.second
            ->bindAsImage(5, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
        m_subjectBuf.second->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_TrixelGlitchSubjects
        );
        const int groupSize = IRPrefab::Glitch::kTrixelGlitchGroupSize;
        IRRender::device()->dispatchCompute(
            static_cast<std::uint32_t>(IRMath::divCeil(m_canvas.size_.x, groupSize)),
            static_cast<std::uint32_t>(IRMath::divCeil(m_canvas.size_.y, groupSize)),
            1
        );
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        // The slot is borrowed: hand it back to the active mask that owns it.
        if (m_slotOwner != nullptr) {
            m_slotOwner->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_VoxelActiveMask);
        }
    }

    void swapColors() override {
        m_canvas.swapGlitchScratchColors();
    }

  private:
    IRComponents::C_TriangleCanvasTextures &m_canvas;
    ShaderProgram &m_program;
    std::pair<ResourceId, Buffer *> &m_subjectBuf;
    std::size_t &m_subjectBufBytes;
    Buffer *m_slotOwner;
};

// Canvas-space glitch of the entities carrying `C_GlitchSubject`. One
// full-canvas compute dispatch over the main canvas reads its colour,
// distance and entity-id planes plus a tick-start history, writes a scratch
// colour plane, and swaps it in as the live colour, so no texel reads a value
// the dispatch wrote. Each subject glitches its own trixels and those within
// its spill; two subjects whose bands overlap interfere there. Shares
// `C_GlitchState`'s period tick and seed with `FRAMEBUFFER_GLITCH`. With no
// active subject it uploads, copies, dispatches and swaps nothing.
template <> struct System<TRIXEL_GLITCH> {
    struct GatheredSubject {
        IREntity::EntityId id_ = IREntity::kNullEntity;
        IRComponents::C_GlitchSubject subject_{};
    };

    ShaderProgram *program_ = nullptr;
    std::pair<ResourceId, Buffer *> subjectBuf_{0, nullptr};
    std::size_t subjectBufBytes_ = 0;
    Buffer *slotOwner_ = nullptr;

    // Frame state resolved in beginTick and the tick.
    IRComponents::C_GlitchState state_{};
    IRPrefab::Glitch::Clock clock_{};
    // Capacity persists across frames; cleared, never released.
    std::vector<GatheredSubject> subjects_;
    std::vector<IRPrefab::Glitch::SubjectRecord> records_;
    std::vector<IRMath::uvec4> words_;
    bool readsHistory_ = false;
    bool enabledThisFrame_ = false;

    bool historyValid_ = false;
    std::int64_t lastTick_ = 0;

    // Encoder calls issued, per frame and since registration, plus whether the
    // last enabled frame had smooth-yaw per-axis canvases (whose voxels this
    // pass does not reach); the fixture's probe lines report them.
    int frameUploads_ = 0;
    int frameHistoryCopies_ = 0;
    int frameDispatches_ = 0;
    int frameSwaps_ = 0;
    std::uint64_t imageCreationCount_ = 0;
    std::uint64_t historyCopyCount_ = 0;
    std::uint64_t dispatchCount_ = 0;
    bool perAxisAllocated_ = false;

    void beginTick() {
        frameUploads_ = 0;
        frameHistoryCopies_ = 0;
        frameDispatches_ = 0;
        frameSwaps_ = 0;
        subjects_.clear();
        subjects_.reserve(
            static_cast<std::size_t>(IREntity::countComponents<IRComponents::C_GlitchSubject>())
        );
        const IRComponents::C_GlitchState *state =
            IREntity::singletonOrNull<IRComponents::C_GlitchState>();
        state_ = state != nullptr ? *state : IRComponents::C_GlitchState{};
        clock_ = IRPrefab::Glitch::clockAt(state_, IRPrefab::Glitch::engineTimeSeconds());
    }

    void tick(IREntity::EntityId entity, const IRComponents::C_GlitchSubject &subject) {
        if (IRPrefab::Glitch::isSubjectActive(subject)) {
            subjects_.push_back({entity, subject});
        }
    }

    // Sorts the gathered subjects by id and builds this frame's records and
    // GPU words. False when no subject is active.
    bool prepareFrame() {
        if (subjects_.empty()) {
            return false;
        }
        std::sort(
            subjects_.begin(),
            subjects_.end(),
            [](const GatheredSubject &a, const GatheredSubject &b) { return a.id_ < b.id_; }
        );
        const IRPrefab::Glitch::TickWords tick = IRPrefab::Glitch::toTickWords(clock_.tick_);
        records_.clear();
        records_.reserve(subjects_.size());
        readsHistory_ = false;
        for (const GatheredSubject &gathered : subjects_) {
            records_.push_back(
                IRPrefab::Glitch::subjectRecord(gathered.id_, gathered.subject_, state_.seed_, tick)
            );
            readsHistory_ =
                readsHistory_ || IRPrefab::Glitch::subjectReadsHistory(gathered.subject_);
        }
        IRPrefab::Glitch::packSubjectWords(records_, state_.seed_, tick, words_);
        return true;
    }

    // Off: no scope, upload, copy, dispatch or swap. The live row reads zero
    // rather than the last enabled sample, and history is reseeded on the
    // next frame that reads it.
    void offFrame() {
        enabledThisFrame_ = false;
        historyValid_ = false;
        IRRender::gpuStageTiming().trixelGlitchMs_ = 0.0f;
    }

    // One enabled frame through @p encoder. History snapshots are always the
    // pre-glitch planes, so effects never accumulate frame over frame.
    void encodeFrame(TrixelGlitchEncoder &encoder, bool imagesAllocated) {
        enabledThisFrame_ = true;
        const TrixelGlitchFramePlan plan = planTrixelGlitchFrame(
            true,
            readsHistory_,
            imagesAllocated,
            historyValid_,
            clock_.tick_,
            lastTick_
        );
        if (plan.createImages_) {
            encoder.createImages();
            historyValid_ = false;
            ++imageCreationCount_;
        }
        encoder.uploadSubjects(words_);
        ++frameUploads_;
        {
            IRRender::GpuSubStageScope scope("trixelGlitch");
            if (plan.copyHistory_) {
                encoder.copyHistoryColors();
                encoder.copyHistoryEntityIds();
                historyValid_ = true;
                lastTick_ = clock_.tick_;
                frameHistoryCopies_ += 2;
                historyCopyCount_ += 2;
            }
            encoder.dispatch();
        }
        ++frameDispatches_;
        ++dispatchCount_;
        encoder.swapColors();
        ++frameSwaps_;
        if (!readsHistory_) {
            historyValid_ = false;
        }
    }

    void endTick() {
        if (!prepareFrame()) {
            offFrame();
            return;
        }
        IR_PROFILE_SCOPE("trixelGlitch");
        const IREntity::EntityId mainCanvas = IRRender::getCanvas("main");
        if (mainCanvas == IREntity::kNullEntity) {
            offFrame();
            return;
        }
        auto &canvas = IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(mainCanvas);
        const auto perAxis =
            IREntity::getComponentOptional<IRComponents::C_PerAxisTrixelCanvases>(mainCanvas);
        perAxisAllocated_ = perAxis.has_value() && perAxis.value()->isAllocated();
        if (slotOwner_ == nullptr) {
            slotOwner_ = IRRender::getNamedResourceOrNull<Buffer>("VoxelActiveMaskBuffer");
        }
        TrixelGlitchGpuEncoder
            encoder{canvas, *program_, subjectBuf_, subjectBufBytes_, slotOwner_};
        encodeFrame(encoder, canvas.hasGlitchImages());
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "TrixelGlitchProgram",
            std::vector{ShaderStage{IRRender::kFileCompTrixelGlitch, ShaderType::COMPUTE}}
        );
        SystemId id = registerSystem<TRIXEL_GLITCH, IRComponents::C_GlitchSubject>("TrixelGlitch");
        auto *sys = getSystemParams<System<TRIXEL_GLITCH>>(id);
        sys->program_ = IRRender::getNamedResource<ShaderProgram>("TrixelGlitchProgram");
        // Deliberately untagged: `encodeFrame`'s GpuSubStageScope owns the
        // `trixelGlitch` row, and a system tag would share its timestamp
        // attachment.
        return id;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_TRIXEL_GLITCH_H */
