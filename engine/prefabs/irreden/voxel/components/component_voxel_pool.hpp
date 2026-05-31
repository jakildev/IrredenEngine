#ifndef COMPONENT_VOXEL_POOL_H
#define COMPONENT_VOXEL_POOL_H

#include <irreden/ir_math.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/voxel_pool_allocation.hpp>

#include <irreden/voxel/components/component_voxel.hpp>

#include <cstdint>
#include <span>
#include <optional>
#include <map>
#include <limits>

using namespace IRMath;
using IREntity::EntityId;

namespace IRComponents {

// Each mask word covers `kVoxelActiveMaskBits` consecutive voxel slots.
// CPU-side storage is `std::vector<uint32_t>`; the GPU compact shader
// reads the same memory through a `uint activeMask[]` SSBO at
// `kBufferIndex_VoxelActiveMask`.
constexpr std::size_t kVoxelActiveMaskBits = 32;

struct ChunkBounds {
    vec2 isoMin_ = vec2(std::numeric_limits<float>::max());
    vec2 isoMax_ = vec2(std::numeric_limits<float>::lowest());

    void expand(vec2 isoPos) {
        isoMin_ = glm::min(isoMin_, isoPos);
        isoMax_ = glm::max(isoMax_, isoPos);
    }

    void reset() {
        isoMin_ = vec2(std::numeric_limits<float>::max());
        isoMax_ = vec2(std::numeric_limits<float>::lowest());
    }
};

struct C_VoxelPool {
  public:
    C_VoxelPool(ivec3 numVoxels)
        : m_voxelPoolSize{numVoxels.x * numVoxels.y * numVoxels.z}
        , m_voxelPositions{}
        , m_voxelPositionsOffset{}
        , m_voxelPositionsGlobal{}
        , m_voxelColors{}
        , m_entityIdsDirty{true}
        , m_chunkBoundsDirty{true} {

        m_voxelPositions.resize(m_voxelPoolSize);
        std::fill(
            m_voxelPositions.begin(),
            m_voxelPositions.end(),
            IRRender::VoxelGpuPosition{vec3(0, 0, 0), 0.0f}
        );

        m_voxelPositionsOffset.resize(m_voxelPoolSize);
        std::fill(m_voxelPositionsOffset.begin(), m_voxelPositionsOffset.end(), vec3(0, 0, 0));

        m_voxelPositionsGlobal.resize(m_voxelPoolSize);
        std::fill(
            m_voxelPositionsGlobal.begin(),
            m_voxelPositionsGlobal.end(),
            IRRender::VoxelGpuPosition{vec3(0, 0, 0), 0.0f}
        );

        m_voxelColors.resize(m_voxelPoolSize);
        std::fill(m_voxelColors.begin(), m_voxelColors.end(), C_Voxel{Color{0, 0, 0, 0}});

        m_voxelEntities.resize(m_voxelPoolSize);
        std::fill(m_voxelEntities.begin(), m_voxelEntities.end(), IREntity::kNullEntity);

        const std::size_t maskWords =
            (static_cast<std::size_t>(m_voxelPoolSize) + kVoxelActiveMaskBits - 1) /
            kVoxelActiveMaskBits;
        m_activeMask.assign(maskWords, 0u);
    }

    C_VoxelPool() {}

    // EntityId addVoxel

    IRRender::VoxelPoolAllocation allocateVoxels(unsigned int size) {
        auto freeSpan = findFreeSpan(size);
        if (freeSpan.has_value()) {
            size_t startIndex = freeSpan->first;
            IRE_LOG_DEBUG("Reusing existing span from {} to {}", startIndex, startIndex + size - 1);
            m_freeSpanLookup[size].erase(*freeSpan);
            if (m_freeSpanLookup[size].empty()) {
                m_freeSpanLookup.erase(size);
            }
            m_chunkBoundsDirty = true;
            return IRRender::VoxelPoolAllocation{
                startIndex,
                std::span<IRRender::VoxelGpuPosition>{m_voxelPositions.data() + startIndex, size},
                std::span<vec3>{m_voxelPositionsOffset.data() + startIndex, size},
                std::span<IRRender::VoxelGpuPosition>{
                    m_voxelPositionsGlobal.data() + startIndex,
                    size
                },
                std::span<C_Voxel>{m_voxelColors.data() + startIndex, size}
            };
        }

        if (m_voxelPoolIndex + size <= m_voxelPoolSize) {
            size_t startIndex = static_cast<size_t>(m_voxelPoolIndex);
            m_voxelPoolIndex += size;
            m_chunkBoundsDirty = true;
            IRE_LOG_DEBUG("Allocated voxels from {} to {}", startIndex, m_voxelPoolIndex - 1);
            return IRRender::VoxelPoolAllocation{
                startIndex,
                std::span<IRRender::VoxelGpuPosition>{m_voxelPositions.data() + startIndex, size},
                std::span<vec3>{m_voxelPositionsOffset.data() + startIndex, size},
                std::span<IRRender::VoxelGpuPosition>{
                    m_voxelPositionsGlobal.data() + startIndex,
                    size
                },
                std::span<C_Voxel>{m_voxelColors.data() + startIndex, size}
            };
        }

        IR_ASSERT(false, "Ran out of voxels");

        return IRRender::VoxelPoolAllocation{
            0,
            std::span<IRRender::VoxelGpuPosition>{},
            std::span<vec3>{},
            std::span<IRRender::VoxelGpuPosition>{},
            std::span<C_Voxel>{}
        };
    }

    void deallocateVoxels(size_t startIndex, size_t size) {
        IR_ASSERT(
            startIndex + size <= static_cast<size_t>(m_voxelPoolSize),
            "deallocateVoxels out of bounds: startIndex={}, size={}, poolSize={}",
            startIndex,
            size,
            m_voxelPoolSize
        );
        for (size_t i = 0; i < size; i++) {
            m_voxelColors[startIndex + i].color_ = Color{0, 0, 0, 0};
        }
        clearActiveMaskRange(startIndex, size);

        std::fill(
            m_voxelEntities.begin() + startIndex,
            m_voxelEntities.begin() + startIndex + size,
            IREntity::kNullEntity
        );
        m_entityIdsDirty = true;
        m_chunkBoundsDirty = true;

        m_freeVoxelSpans.push_back({startIndex, size});
        updateFreeSpanLookup(startIndex, size);
    }

    std::vector<IRRender::VoxelGpuPosition> &getPositions() {
        return m_voxelPositions;
    }
    std::vector<vec3> &getPositionOffsets() {
        return m_voxelPositionsOffset;
    }
    std::vector<IRRender::VoxelGpuPosition> &getPositionGlobals() {
        return m_voxelPositionsGlobal;
    }
    std::vector<C_Voxel> &getColors() {
        return m_voxelColors;
    }

    const std::vector<IRRender::VoxelGpuPosition> &getPositions() const {
        return m_voxelPositions;
    }

    const std::vector<vec3> &getPositionOffsets() const {
        return m_voxelPositionsOffset;
    }

    const std::vector<IRRender::VoxelGpuPosition> &getPositionGlobals() const {
        return m_voxelPositionsGlobal;
    }

    const std::vector<C_Voxel> &getColors() const {
        return m_voxelColors;
    }

    int getVoxelPoolSize() const {
        return m_voxelPoolSize;
    }
    int getLiveVoxelCount() const {
        return m_voxelPoolIndex;
    }
    ivec3 getVoxelPoolSize3D() const {
        return m_voxelPoolSize3D;
    }

    void setEntityIdForRange(size_t startIdx, size_t count, EntityId entityId) {
        IR_ASSERT(
            startIdx + count <= m_voxelEntities.size(),
            "setEntityIdForRange out of bounds: startIdx={}, count={}, poolSize={}",
            startIdx,
            count,
            m_voxelEntities.size()
        );
        std::fill(
            m_voxelEntities.begin() + startIdx,
            m_voxelEntities.begin() + startIdx + count,
            entityId
        );
        m_entityIdsDirty = true;
    }

    const std::vector<EntityId> &getEntityIds() const {
        return m_voxelEntities;
    }

    bool isEntityIdsDirty() const {
        return m_entityIdsDirty;
    }

    void clearEntityIdsDirty() {
        m_entityIdsDirty = false;
    }

    [[deprecated(
        "Capture VoxelPoolAllocation::startIndex_ at allocateVoxels call time instead — see "
        "engine/render/CLAUDE.md"
    )]]
    const IRRender::VoxelGpuPosition *getPositionGlobalsBasePtr() const {
        return m_voxelPositionsGlobal.data();
    }

    int getChunkCount() const {
        return (m_voxelPoolIndex + IRRender::kVoxelChunkSize - 1) / IRRender::kVoxelChunkSize;
    }

    std::vector<ChunkBounds> &getChunkBounds() {
        return m_chunkBounds;
    }

    // @p useContinuousYaw (smooth camera Z-yaw, T3 / #1310): while the per-axis
    // canvases are active the framebuffer scatter rasterizes voxels at their
    // CONTINUOUS yawed iso position, so the chunk-visibility gate must project
    // the same way — the cardinal-snapped chunk bounds otherwise drop off-center
    // chunks (the "missing objects / ground during rotation" symptom). The
    // continuous-yaw bounds change every frame, so they bypass the cardinal-
    // index cache and force a fresh recompute on the next cardinal-path call.
    void rebuildChunkBounds(
        CardinalIndex cardinalIndex = CardinalIndex::k0,
        bool useContinuousYaw = false,
        float visualYaw = 0.0f
    ) {
        if (!useContinuousYaw && !m_chunkBoundsDirty && cardinalIndex == m_lastBoundsCardinalIndex)
            return;

        int chunkCount = getChunkCount();
        m_chunkBounds.resize(chunkCount);
        for (auto &cb : m_chunkBounds)
            cb.reset();

        for (int i = 0; i < m_voxelPoolIndex; ++i) {
            if (m_voxelColors[i].color_.alpha_ == 0)
                continue;
            int chunk = i / IRRender::kVoxelChunkSize;
            vec3 pos = m_voxelPositionsGlobal[i].pos_;
            vec2 isoPos;
            if (useContinuousYaw) {
                isoPos = IRMath::pos3DtoPos2DIsoYawed(pos, visualYaw);
            } else {
                if (cardinalIndex != CardinalIndex::k0) {
                    pos = IRMath::rotateCardinalZ(pos, cardinalIndex);
                    pos += vec3(IRMath::cardinalLowerCornerShift(cardinalIndex));
                }
                isoPos = IRMath::pos3DtoPos2DIso(pos);
            }
            m_chunkBounds[chunk].expand(isoPos);
        }
        if (useContinuousYaw) {
            // Never cache a per-frame yaw snapshot; force the next cardinal-path
            // call to rebuild rather than trust stale continuous bounds.
            m_chunkBoundsDirty = true;
        } else {
            m_lastBoundsCardinalIndex = cardinalIndex;
            m_chunkBoundsDirty = false;
        }
    }

    void markChunkBoundsDirty() {
        m_chunkBoundsDirty = true;
    }

    // Cull query: true if any chunk overlapping the pool slot range
    // [startIdx, startIdx + count) has an iso-space AABB that intersects
    // `viewport`. Reads the chunk bounds as last computed by
    // `rebuildChunkBounds` (driven by the render pipeline) — a caller in
    // the UPDATE pipeline gets a one-frame-lagged but self-consistent
    // answer (both the bounds and the cull viewport are last-render state).
    // Chunk granularity is conservative: a chunk shared by several voxel
    // sets reports visible if ANY of its voxels are in view, so the gate
    // over-rebuilds rather than dropping geometry. Fail-safe: returns true
    // when the bounds aren't available yet (pre-first-render) or the range
    // runs past the computed chunk set, so geometry is never silently
    // culled before bounds exist. The intersection test mirrors
    // `buildChunkVisibilityMask` in `system_voxel_to_trixel.hpp`.
    bool
    isRangeVisible(std::size_t startIdx, std::size_t count, const IsoBounds2D &viewport) const {
        if (count == 0) {
            return false;
        }
        if (m_chunkBounds.empty()) {
            return true;
        }
        const std::size_t firstChunk = startIdx / IRRender::kVoxelChunkSize;
        const std::size_t lastChunk = (startIdx + count - 1) / IRRender::kVoxelChunkSize;
        for (std::size_t c = firstChunk; c <= lastChunk; ++c) {
            if (c >= m_chunkBounds.size()) {
                return true;
            }
            const ChunkBounds &cb = m_chunkBounds[c];
            if (cb.isoMax_.x >= viewport.min_.x && cb.isoMin_.x <= viewport.max_.x &&
                cb.isoMax_.y >= viewport.min_.y && cb.isoMin_.y <= viewport.max_.y) {
                return true;
            }
        }
        return false;
    }

    // Active-slot mask: 1 bit per pool slot, mirroring `m_voxelColors[i].color_.alpha_ != 0`.
    // The GPU compact shader at `c_voxel_visibility_compact.{glsl,metal}` reads this in place
    // of the per-voxel alpha test (T-287 / #950). CPU storage is `std::vector<uint32_t>`; the
    // shader binds it as `uint activeMask[]` at `kBufferIndex_VoxelActiveMask`.
    const std::vector<std::uint32_t> &getActiveMask() const {
        return m_activeMask;
    }
    std::size_t getActiveMaskSizeBytes() const {
        return m_activeMask.size() * sizeof(std::uint32_t);
    }

    void setActiveBit(std::size_t idx) {
        IR_ASSERT(
            idx < static_cast<std::size_t>(m_voxelPoolSize),
            "setActiveBit out of bounds: idx={}, poolSize={}",
            idx,
            m_voxelPoolSize
        );
        m_activeMask[idx / kVoxelActiveMaskBits] |=
            (std::uint32_t{1} << (idx % kVoxelActiveMaskBits));
    }

    void clearActiveBit(std::size_t idx) {
        IR_ASSERT(
            idx < static_cast<std::size_t>(m_voxelPoolSize),
            "clearActiveBit out of bounds: idx={}, poolSize={}",
            idx,
            m_voxelPoolSize
        );
        m_activeMask[idx / kVoxelActiveMaskBits] &=
            ~(std::uint32_t{1} << (idx % kVoxelActiveMaskBits));
    }

    // Bulk variants for span-shaped mutations on `C_VoxelSetNew`. The single-bit
    // setters above use one OR/AND per word touched; the range variants below
    // handle the partial-word prefix and suffix once and mass-write the middle
    // words to all-ones / zeros.
    void setActiveMaskRange(std::size_t start, std::size_t count) {
        setMaskRange(start, count, true);
    }

    void clearActiveMaskRange(std::size_t start, std::size_t count) {
        setMaskRange(start, count, false);
    }

    // Recompute the mask bits for `[start, start + count)` from the live
    // `m_voxelColors[i].color_.alpha_` values. Use after a span-shaped write
    // that mixes active and inactive slots (e.g. `C_VoxelSetNew`'s ctor fill
    // or `reshape`/`fillPlane`) — saves the caller from a per-voxel
    // `setActiveBit` / `clearActiveBit` choice.
    void resyncActiveMaskFromColors(std::size_t start, std::size_t count) {
        IR_ASSERT(
            start + count <= static_cast<std::size_t>(m_voxelPoolSize),
            "resyncActiveMaskFromColors out of bounds: start={}, count={}, poolSize={}",
            start,
            count,
            m_voxelPoolSize
        );
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t idx = start + i;
            if (m_voxelColors[idx].color_.alpha_ != 0) {
                setActiveBit(idx);
            } else {
                clearActiveBit(idx);
            }
        }
    }

    // Cap on queued position ranges. The fixed-timestep loop runs
    // UPDATE_VOXEL_SET_CHILDREN once per update tick, and a slow render
    // frame accumulates several update ticks before VOXEL_TO_TRIXEL_STAGE_1
    // drains the queue. Every moving voxel set re-queues its range each
    // tick, so the list can reach (update ticks) × (moving sets) entries —
    // millions in a stress scene. Past this cap the sort + coalesce in
    // `flushPendingPositionRanges` costs far more than one whole-live-range
    // upload, so the flusher treats a saturated queue as "re-upload
    // everything" and further queue calls become no-ops (the full upload
    // covers the dropped ranges anyway).
    static constexpr std::size_t kMaxPendingPositionRanges = 8192;

    // Queue a slice of position-globals to upload to the GPU position
    // SSBO on the next `flushPendingPositionRanges` call. Mirrors the
    // pending-list-flush pattern documented in `cpp-ecs.md`: the
    // mutating system (UPDATE_VOXEL_SET_CHILDREN) queues the affected
    // slice; the GPU-buffer-owning system (VOXEL_TO_TRIXEL_STAGE_1)
    // coalesces contiguous queued ranges into one `subData` per run.
    // Saturating `kMaxPendingPositionRanges` switches the flusher to a
    // single whole-buffer upload — see `flushPendingPositionRanges`.
    void queuePositionRange(size_t startIdx, size_t count) {
        if (count == 0) {
            return;
        }
        if (m_pendingPositionRanges.size() >= kMaxPendingPositionRanges) {
            return;
        }
        m_pendingPositionRanges.emplace_back(startIdx, count);
    }

    const std::vector<std::pair<size_t, size_t>> &getPendingPositionRanges() const {
        return m_pendingPositionRanges;
    }

    std::vector<std::pair<size_t, size_t>> &getPendingPositionRanges() {
        return m_pendingPositionRanges;
    }

    void clearPendingPositionRanges() {
        m_pendingPositionRanges.clear();
    }

  private:
    int m_voxelPoolSize;
    ivec3 m_voxelPoolSize3D;
    bool m_entityIdsDirty = true;
    bool m_chunkBoundsDirty = true;
    CardinalIndex m_lastBoundsCardinalIndex = CardinalIndex::k0;

    std::vector<EntityId> m_voxelEntities;
    std::vector<IRRender::VoxelGpuPosition> m_voxelPositions;
    std::vector<vec3> m_voxelPositionsOffset;
    std::vector<IRRender::VoxelGpuPosition> m_voxelPositionsGlobal;
    std::vector<C_Voxel> m_voxelColors;
    std::vector<std::uint32_t> m_activeMask;
    std::vector<std::pair<size_t, size_t>> m_freeVoxelSpans;
    std::map<size_t, std::set<std::pair<size_t, size_t>>> m_freeSpanLookup;
    std::vector<ChunkBounds> m_chunkBounds;
    // Per-frame queue of position-global slices whose CPU contents were
    // rewritten since the last GPU flush. Drained + coalesced by
    // VOXEL_TO_TRIXEL_STAGE_1; capacity is preserved across frames so
    // a steady-state moving scene avoids per-frame allocation.
    std::vector<std::pair<size_t, size_t>> m_pendingPositionRanges;

    int m_voxelPoolIndex = 0;

    void updateFreeSpanLookup(size_t startIndex, size_t size) {
        m_freeSpanLookup[size].insert({startIndex, size});
    }

    void setMaskRange(std::size_t start, std::size_t count, bool value) {
        if (count == 0) {
            return;
        }
        IR_ASSERT(
            start + count <= static_cast<std::size_t>(m_voxelPoolSize),
            "setMaskRange out of bounds: start={}, count={}, poolSize={}",
            start,
            count,
            m_voxelPoolSize
        );
        const std::size_t end = start + count;
        std::size_t idx = start;
        // Partial-word prefix and a possible all-in-one-word path.
        while (idx < end && (idx % kVoxelActiveMaskBits) != 0) {
            if (value) {
                setActiveBit(idx);
            } else {
                clearActiveBit(idx);
            }
            ++idx;
        }
        // Whole-word middle.
        while (idx + kVoxelActiveMaskBits <= end) {
            m_activeMask[idx / kVoxelActiveMaskBits] =
                value ? std::numeric_limits<std::uint32_t>::max() : 0u;
            idx += kVoxelActiveMaskBits;
        }
        // Partial-word suffix.
        while (idx < end) {
            if (value) {
                setActiveBit(idx);
            } else {
                clearActiveBit(idx);
            }
            ++idx;
        }
    }

    std::optional<std::pair<size_t, size_t>> findFreeSpan(size_t requestedSize) const {
        auto it = m_freeSpanLookup.lower_bound(requestedSize);
        if (it != m_freeSpanLookup.end()) {
            std::pair<size_t, size_t> span = *it->second.begin();
            // Return the span if it's not larger than needed
            if (span.second <= requestedSize) {
                return span;
            }
        }
        return std::nullopt; // No suitable free span found
    }
};
} // namespace IRComponents

#endif /* COMPONENT_VOXEL_POOL_H */
