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
#include <irreden/render/per_axis_canvas.hpp>
#include <irreden/render/trixel_glitch_math.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
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

struct TrixelGlitchPerAxisFrameInput {
    bool allocated_ = false;
    bool scratchAllocated_ = false;
    bool historyAllocated_ = false;
    IRMath::ivec2 size_{0};
    IRMath::ivec4 storeFrame_{0};
    int depthScale_ = 1;
    int depthBias_ = 0;
};

struct TrixelGlitchPerAxisFramePlan {
    bool enabled_ = false;
    bool createScratch_ = false;
    bool createHistory_ = false;
    bool copyHistory_ = false;
};

inline TrixelGlitchPerAxisFramePlan planTrixelGlitchPerAxisFrame(
    const TrixelGlitchPerAxisFrameInput &input,
    bool disabled,
    bool readsHistory,
    bool historyValid,
    std::int64_t tick,
    std::int64_t lastTick,
    IRMath::ivec2 historySize,
    int historyCardinal
) {
    if (!input.allocated_ || disabled) {
        return {};
    }
    const bool createHistory = readsHistory && !input.historyAllocated_;
    const bool historyMatches =
        historySize == input.size_ && historyCardinal == input.storeFrame_.z;
    return {
        true,
        !input.scratchAllocated_,
        createHistory,
        readsHistory && (createHistory || !historyValid || tick != lastTick || !historyMatches)
    };
}

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
// history copies on a new tick, `dispatch`, then `swapColors`. The per-axis
// route clears its indirect args, marks reached tiles, resolves them into
// scratch, and writes them back without swapping a live handle. Nothing ever
// writes the distance or entity-id planes. `TrixelGlitchGpuEncoder` encodes it;
// the headless probes record it.
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
    virtual void createPerAxisResources() = 0;
    virtual void createPerAxisHistory() = 0;
    virtual void copyPerAxisHistoryColors(int axis) = 0;
    virtual void copyPerAxisHistoryEntityIds(int axis) = 0;
    virtual void clearPerAxisArgs() = 0;
    virtual void dispatchPerAxisMark(int axis) = 0;
    virtual void dispatchPerAxisResolve(int axis) = 0;
    virtual void dispatchPerAxisWriteback(int axis) = 0;
    virtual void finishPerAxisDispatches() = 0;

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
        Buffer *slotOwner,
        IRComponents::C_PerAxisTrixelCanvases *perAxis,
        ShaderProgram *perAxisMarkProgram,
        ShaderProgram *perAxisResolveProgram,
        ShaderProgram *perAxisWritebackProgram,
        Buffer *&voxelCompactedBuf,
        Buffer *&voxelIndirectBuf,
        Buffer *lightingFrameDataBuf,
        Buffer *sunDepthBuf,
        Buffer *sunFrameDataBuf
    )
        : m_canvas{canvas}
        , m_program{program}
        , m_subjectBuf{subjectBuf}
        , m_subjectBufBytes{subjectBufBytes}
        , m_slotOwner{slotOwner}
        , m_perAxis{perAxis}
        , m_perAxisMarkProgram{perAxisMarkProgram}
        , m_perAxisResolveProgram{perAxisResolveProgram}
        , m_perAxisWritebackProgram{perAxisWritebackProgram}
        , m_voxelCompactedBuf{voxelCompactedBuf}
        , m_voxelIndirectBuf{voxelIndirectBuf}
        , m_lightingFrameDataBuf{lightingFrameDataBuf}
        , m_sunDepthBuf{sunDepthBuf}
        , m_sunFrameDataBuf{sunFrameDataBuf} {}

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

    void createPerAxisResources() override {
        for (auto &axis : m_perAxis->axes_) {
            axis.createGlitchScratch(m_perAxis->size_);
        }
        m_perAxis->createGlitchWorklist();
    }

    void createPerAxisHistory() override {
        for (auto &axis : m_perAxis->axes_) {
            axis.createGlitchHistory(m_perAxis->size_);
        }
    }

    void copyPerAxisHistoryColors(int axis) override {
        auto &textures = m_perAxis->axes_[axis];
        IRRender::device()->copyTexture2D(
            *textures.colors_.second,
            *textures.glitchHistoryColors_.second
        );
    }

    void copyPerAxisHistoryEntityIds(int axis) override {
        auto &textures = m_perAxis->axes_[axis];
        IRRender::device()->copyTexture2D(
            *textures.entityIds_.second,
            *textures.glitchHistoryEntityIds_.second
        );
    }

    void clearPerAxisArgs() override {
        IRRender::device()->fillBuffer(
            m_perAxis->glitchTileArgs_.second,
            static_cast<std::size_t>(IRComponents::PerAxisCanvasStore::kAxisCount) *
                IRComponents::PerAxisCanvasStore::kScratchAlignUints * sizeof(std::uint32_t),
            std::uint8_t{0}
        );
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
    }

    void bindPerAxisWorklist(int axis) {
        const std::ptrdiff_t regionOffset = static_cast<std::ptrdiff_t>(axis) *
                                            m_perAxis->glitchTileRegionStride_ *
                                            static_cast<int>(sizeof(std::uint32_t));
        const std::size_t regionBytes =
            static_cast<std::size_t>(m_perAxis->glitchTileRegionStride_) * sizeof(std::uint32_t);
        m_perAxis->glitchTileFlags_.second->bindRange(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_TrixelGlitchTileFlags,
            regionOffset,
            regionBytes
        );
        m_perAxis->glitchTileList_.second->bindRange(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_TrixelGlitchTileList,
            regionOffset,
            regionBytes
        );
        m_perAxis->glitchTileArgs_.second->bindRange(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_TrixelGlitchTileArgs,
            static_cast<std::ptrdiff_t>(axis) *
                IRComponents::PerAxisCanvasStore::kScratchAlignUints *
                static_cast<int>(sizeof(std::uint32_t)),
            static_cast<std::size_t>(IRComponents::PerAxisCanvasStore::kScratchAlignUints) *
                sizeof(std::uint32_t)
        );
    }

    void dispatchPerAxisMark(int axis) override {
        auto &textures = m_perAxis->axes_[axis];
        m_perAxisMarkProgram->use();
        textures.entityIds_.second->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
        m_subjectBuf.second->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_TrixelGlitchSubjects
        );
        m_perAxis->cellCompacted_.second->bindRange(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisCellCompacted,
            static_cast<std::ptrdiff_t>(axis) * m_perAxis->cellRegionStride_ *
                static_cast<int>(sizeof(std::uint32_t)),
            static_cast<std::size_t>(m_perAxis->cellRegionStride_) * sizeof(std::uint32_t)
        );
        m_perAxis->cellIndirect_.second->bindRange(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisCellIndirect,
            static_cast<std::ptrdiff_t>(axis) * kPerAxisCellIndirectStrideBytes,
            kPerAxisCellIndirectStrideBytes
        );
        bindPerAxisWorklist(axis);
        IRRender::device()->dispatchComputeIndirect(
            m_perAxis->cellIndirect_.second,
            static_cast<std::ptrdiff_t>(axis) * kPerAxisCellIndirectStrideBytes +
                kPerAxisCellDispatchArgsOffsetBytes
        );
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        IRRender::device()->memoryBarrier(BarrierType::COMMAND);
    }

    void dispatchPerAxisResolve(int axis) override {
        auto &textures = m_perAxis->axes_[axis];
        m_perAxisResolveProgram->use();
        textures.colors_.second->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        textures.distances_.second->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
        textures.entityIds_.second->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
        (textures.hasGlitchHistory() ? textures.glitchHistoryColors_.second
                                     : textures.colors_.second)
            ->bindAsImage(3, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        (textures.hasGlitchHistory() ? textures.glitchHistoryEntityIds_.second
                                     : textures.entityIds_.second)
            ->bindAsImage(4, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
        textures.glitchScratchColors_.second
            ->bindAsImage(5, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
        m_subjectBuf.second->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_TrixelGlitchSubjects
        );
        bindPerAxisWorklist(axis);
        IRRender::device()->dispatchComputeIndirect(
            m_perAxis->glitchTileArgs_.second,
            static_cast<std::ptrdiff_t>(axis) *
                IRComponents::PerAxisCanvasStore::kScratchAlignUints *
                static_cast<int>(sizeof(std::uint32_t))
        );
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
    }

    void dispatchPerAxisWriteback(int axis) override {
        auto &textures = m_perAxis->axes_[axis];
        m_perAxisWritebackProgram->use();
        textures.colors_.second->bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
        textures.entityIds_.second->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
        textures.glitchScratchColors_.second
            ->bindAsImage(5, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        bindPerAxisWorklist(axis);
        IRRender::device()->dispatchComputeIndirect(
            m_perAxis->glitchTileArgs_.second,
            static_cast<std::ptrdiff_t>(axis) *
                IRComponents::PerAxisCanvasStore::kScratchAlignUints *
                static_cast<int>(sizeof(std::uint32_t))
        );
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
    }

    void finishPerAxisDispatches() override {
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        IRPrefab::PerAxisCanvas::restoreVoxelCompactionSlots(
            m_voxelCompactedBuf,
            m_voxelIndirectBuf
        );
        if (m_slotOwner != nullptr) {
            m_slotOwner->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_VoxelActiveMask);
        }
        if (m_lightingFrameDataBuf != nullptr) {
            m_lightingFrameDataBuf->bindBase(
                BufferTarget::UNIFORM,
                kBufferIndex_FrameDataLightingToTrixel
            );
        }
        if (m_sunDepthBuf != nullptr) {
            m_sunDepthBuf->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        }
        if (m_sunFrameDataBuf != nullptr) {
            m_sunFrameDataBuf->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);
        }
    }

  private:
    IRComponents::C_TriangleCanvasTextures &m_canvas;
    ShaderProgram &m_program;
    std::pair<ResourceId, Buffer *> &m_subjectBuf;
    std::size_t &m_subjectBufBytes;
    Buffer *m_slotOwner;
    IRComponents::C_PerAxisTrixelCanvases *m_perAxis;
    ShaderProgram *m_perAxisMarkProgram;
    ShaderProgram *m_perAxisResolveProgram;
    ShaderProgram *m_perAxisWritebackProgram;
    Buffer *&m_voxelCompactedBuf;
    Buffer *&m_voxelIndirectBuf;
    Buffer *m_lightingFrameDataBuf;
    Buffer *m_sunDepthBuf;
    Buffer *m_sunFrameDataBuf;
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
    ShaderProgram *perAxisMarkProgram_ = nullptr;
    ShaderProgram *perAxisResolveProgram_ = nullptr;
    ShaderProgram *perAxisWritebackProgram_ = nullptr;
    std::pair<ResourceId, Buffer *> subjectBuf_{0, nullptr};
    std::size_t subjectBufBytes_ = 0;
    Buffer *slotOwner_ = nullptr;
    Buffer *voxelCompactedBuf_ = nullptr;
    Buffer *voxelIndirectBuf_ = nullptr;
    Buffer *lightingFrameDataBuf_ = nullptr;
    Buffer *sunDepthBuf_ = nullptr;
    Buffer *sunFrameDataBuf_ = nullptr;
    bool perAxisDisabled_ = false;

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
    bool perAxisHistoryValid_ = false;
    std::int64_t perAxisLastTick_ = 0;
    IRMath::ivec2 perAxisHistoryOrigin_{0};
    IRMath::ivec2 perAxisHistorySize_{0};
    int perAxisHistoryCardinal_ = -1;

    // Encoder calls issued, per frame and since registration, plus whether the
    // last enabled frame had smooth-yaw per-axis canvases (whose voxels this
    // pass does not reach); the fixture's probe lines report them.
    int frameUploads_ = 0;
    int frameHistoryCopies_ = 0;
    int frameDispatches_ = 0;
    int frameSwaps_ = 0;
    int framePerAxisHistoryCopies_ = 0;
    int framePerAxisDispatches_ = 0;
    int framePerAxisMarkDispatches_ = 0;
    int framePerAxisResolveDispatches_ = 0;
    int framePerAxisWritebackDispatches_ = 0;
    int framePerAxisFills_ = 0;
    int framePerAxisSwaps_ = 0;
    std::uint64_t imageCreationCount_ = 0;
    std::uint64_t perAxisScratchCreationCount_ = 0;
    std::uint64_t perAxisHistoryCreationCount_ = 0;
    std::uint64_t perAxisHistoryCopyCount_ = 0;
    std::uint64_t perAxisDispatchCount_ = 0;
    std::uint64_t historyCopyCount_ = 0;
    std::uint64_t dispatchCount_ = 0;
    bool perAxisAllocated_ = false;

    void beginTick() {
        frameUploads_ = 0;
        frameHistoryCopies_ = 0;
        frameDispatches_ = 0;
        frameSwaps_ = 0;
        framePerAxisHistoryCopies_ = 0;
        framePerAxisDispatches_ = 0;
        framePerAxisMarkDispatches_ = 0;
        framePerAxisResolveDispatches_ = 0;
        framePerAxisWritebackDispatches_ = 0;
        framePerAxisFills_ = 0;
        framePerAxisSwaps_ = 0;
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
        perAxisHistoryValid_ = false;
        IRRender::gpuStageTiming().trixelGlitchMs_ = 0.0f;
        IRRender::gpuStageTiming().trixelGlitchPerAxisMs_ = 0.0f;
    }

    // One enabled frame through @p encoder. History snapshots are always the
    // pre-glitch planes, so effects never accumulate frame over frame.
    void encodeFrame(
        TrixelGlitchEncoder &encoder,
        bool imagesAllocated,
        const TrixelGlitchPerAxisFrameInput &perAxisInput = {}
    ) {
        enabledThisFrame_ = true;
        const TrixelGlitchPerAxisFramePlan perAxisPlan = planTrixelGlitchPerAxisFrame(
            perAxisInput,
            perAxisDisabled_,
            readsHistory_,
            perAxisHistoryValid_,
            clock_.tick_,
            perAxisLastTick_,
            perAxisHistorySize_,
            perAxisHistoryCardinal_
        );
        if (perAxisPlan.enabled_) {
            const IRMath::ivec2 snapshotOrigin = perAxisPlan.copyHistory_
                                                     ? IRMath::ivec2(perAxisInput.storeFrame_)
                                                     : perAxisHistoryOrigin_;
            IRPrefab::Glitch::setPerAxisFrameWords(
                words_,
                perAxisInput.storeFrame_,
                snapshotOrigin,
                perAxisInput.depthScale_,
                perAxisInput.depthBias_
            );
        }
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
        if (!perAxisPlan.enabled_) {
            perAxisHistoryValid_ = false;
            IRRender::gpuStageTiming().trixelGlitchPerAxisMs_ = 0.0f;
            return;
        }
        if (perAxisPlan.createScratch_) {
            encoder.createPerAxisResources();
            ++perAxisScratchCreationCount_;
        }
        if (perAxisPlan.createHistory_) {
            encoder.createPerAxisHistory();
            ++perAxisHistoryCreationCount_;
        }
        {
            IRRender::GpuSubStageScope scope("trixelGlitchPerAxis");
            if (perAxisPlan.copyHistory_) {
                for (int axis = 0; axis < IRComponents::PerAxisCanvasStore::kAxisCount; ++axis) {
                    encoder.copyPerAxisHistoryColors(axis);
                    encoder.copyPerAxisHistoryEntityIds(axis);
                    framePerAxisHistoryCopies_ += 2;
                    perAxisHistoryCopyCount_ += 2;
                }
                perAxisHistoryValid_ = true;
                perAxisLastTick_ = clock_.tick_;
                perAxisHistoryOrigin_ = IRMath::ivec2(perAxisInput.storeFrame_);
                perAxisHistorySize_ = perAxisInput.size_;
                perAxisHistoryCardinal_ = perAxisInput.storeFrame_.z;
            }
            encoder.clearPerAxisArgs();
            ++framePerAxisFills_;
            for (int axis = 0; axis < IRComponents::PerAxisCanvasStore::kAxisCount; ++axis) {
                encoder.dispatchPerAxisMark(axis);
                ++framePerAxisMarkDispatches_;
            }
            for (int axis = 0; axis < IRComponents::PerAxisCanvasStore::kAxisCount; ++axis) {
                encoder.dispatchPerAxisResolve(axis);
                ++framePerAxisResolveDispatches_;
            }
            for (int axis = 0; axis < IRComponents::PerAxisCanvasStore::kAxisCount; ++axis) {
                encoder.dispatchPerAxisWriteback(axis);
                ++framePerAxisWritebackDispatches_;
            }
            framePerAxisDispatches_ = framePerAxisMarkDispatches_ + framePerAxisResolveDispatches_ +
                                      framePerAxisWritebackDispatches_;
            perAxisDispatchCount_ += framePerAxisDispatches_;
            encoder.finishPerAxisDispatches();
        }
        if (!readsHistory_) {
            perAxisHistoryValid_ = false;
        }
    }

    void endTick() {
        if (!prepareFrame()) {
            offFrame();
            return;
        }
        const IREntity::EntityId mainCanvas = IRRender::getCanvas("main");
        if (mainCanvas == IREntity::kNullEntity) {
            offFrame();
            return;
        }
        const auto perAxis =
            IREntity::getComponentOptional<IRComponents::C_PerAxisTrixelCanvases>(mainCanvas);
        perAxisAllocated_ = perAxis.has_value() && perAxis.value()->isAllocated();
        IR_PROFILE_SCOPE("trixelGlitch");
        auto &canvas = IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(mainCanvas);
        if (slotOwner_ == nullptr) {
            slotOwner_ = IRRender::getNamedResourceOrNull<Buffer>("VoxelActiveMaskBuffer");
        }
        IRComponents::C_PerAxisTrixelCanvases *axes = perAxisAllocated_ ? perAxis.value() : nullptr;
        TrixelGlitchGpuEncoder encoder{
            canvas,
            *program_,
            subjectBuf_,
            subjectBufBytes_,
            slotOwner_,
            axes,
            perAxisMarkProgram_,
            perAxisResolveProgram_,
            perAxisWritebackProgram_,
            voxelCompactedBuf_,
            voxelIndirectBuf_,
            lightingFrameDataBuf_,
            sunDepthBuf_,
            sunFrameDataBuf_
        };
        TrixelGlitchPerAxisFrameInput perAxisInput;
        if (axes != nullptr) {
            perAxisInput.allocated_ = true;
            perAxisInput.scratchAllocated_ =
                axes->axes_[0].hasGlitchScratch() && axes->hasGlitchWorklist();
            perAxisInput.historyAllocated_ = axes->axes_[0].hasGlitchHistory();
            perAxisInput.size_ = axes->size_;
            perAxisInput.storeFrame_ = IRPrefab::PerAxisCanvas::storeFrame(axes->size_);
            perAxisInput.depthScale_ = IRRender::getVoxelRenderEffectiveSubdivisions();
        }
        encodeFrame(encoder, canvas.hasGlitchImages(), perAxisInput);
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "TrixelGlitchProgram",
            std::vector{ShaderStage{IRRender::kFileCompTrixelGlitch, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "TrixelGlitchPerAxisResolveProgram",
            std::vector{ShaderStage{IRRender::kFileCompTrixelGlitchPerAxis, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "TrixelGlitchPerAxisMarkProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompTrixelGlitchPerAxisMark, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<ShaderProgram>(
            "TrixelGlitchPerAxisWritebackProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompTrixelGlitchPerAxisWriteback, ShaderType::COMPUTE}
            }
        );
        SystemId id = registerSystem<TRIXEL_GLITCH, IRComponents::C_GlitchSubject>("TrixelGlitch");
        auto *sys = getSystemParams<System<TRIXEL_GLITCH>>(id);
        sys->program_ = IRRender::getNamedResource<ShaderProgram>("TrixelGlitchProgram");
        sys->perAxisMarkProgram_ =
            IRRender::getNamedResource<ShaderProgram>("TrixelGlitchPerAxisMarkProgram");
        sys->perAxisResolveProgram_ =
            IRRender::getNamedResource<ShaderProgram>("TrixelGlitchPerAxisResolveProgram");
        sys->perAxisWritebackProgram_ =
            IRRender::getNamedResource<ShaderProgram>("TrixelGlitchPerAxisWritebackProgram");
        sys->lightingFrameDataBuf_ =
            IRRender::getNamedResourceOrNull<Buffer>("LightingToTrixelFrameData");
        sys->sunDepthBuf_ = IRRender::getNamedResourceOrNull<Buffer>("SunShadowDepthMap");
        sys->sunFrameDataBuf_ =
            IRRender::getNamedResourceOrNull<Buffer>("ComputeSunShadowFrameData");
        sys->perAxisDisabled_ = std::getenv("IR_TRIXEL_GLITCH_PER_AXIS_DISABLE") != nullptr;
        // Deliberately untagged: `encodeFrame`'s GpuSubStageScope owns the
        // `trixelGlitch` row, and a system tag would share its timestamp
        // attachment.
        return id;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_TRIXEL_GLITCH_H */
