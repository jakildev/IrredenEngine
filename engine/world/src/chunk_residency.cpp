#include <irreden/world/chunk_residency.hpp>

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/world/chunk_persistence.hpp>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace IRWorld {

namespace {

// Smaller = more urgent. FORCED is the editor "load this chunk now"
// path that bypasses the byte budget; the camera-derived priorities
// drain after it in (VISIBLE_RENDER, PREFETCH_RING) order.
int requestPriorityUrgency(RequestPriority p) {
    switch (p) {
    case RequestPriority::FORCED:
        return 0;
    case RequestPriority::VISIBLE_RENDER:
        return 1;
    case RequestPriority::PREFETCH_RING:
        return 2;
    }
    return 3;
}

IRComponents::C_Voxel recordToVoxel(const IRAsset::VoxelRecord &r) {
    // C_Voxel and VoxelRecord share an identical 12-byte std430 layout
    // but stay distinct types so layout drift surfaces as a compile
    // error (same contract as voxel/dense_bridge.hpp).
    return IRComponents::C_Voxel{r.color_, r.material_id_, r.flags_, r.bone_id_, r.layer_id_};
}

IRAsset::VoxelRecord voxelToRecord(const IRComponents::C_Voxel &v) {
    IRAsset::VoxelRecord r;
    r.color_ = v.color_;
    r.material_id_ = v.material_id_;
    r.flags_ = v.flags_;
    r.bone_id_ = v.bone_id_;
    r.layer_id_ = v.layer_id_;
    return r;
}

std::vector<IRAsset::VoxelRecord> poolSliceToRecords(const IRRender::VoxelPoolAllocation &alloc) {
    std::vector<IRAsset::VoxelRecord> records;
    records.reserve(alloc.voxels_.size());
    for (const auto &v : alloc.voxels_) {
        records.push_back(voxelToRecord(v));
    }
    return records;
}

void seedPoolSliceFromRecords(
    IRRender::VoxelPoolAllocation &alloc, const std::vector<IRAsset::VoxelRecord> &records
) {
    // Caller has already gated on `alloc.voxels_.size() == records.size()`.
    for (std::size_t i = 0; i < records.size(); ++i) {
        alloc.voxels_[i] = recordToVoxel(records[i]);
    }
}

} // namespace

ChunkResidencyManager::ChunkResidencyManager(Config config)
    : m_config{std::move(config)} {}

void ChunkResidencyManager::beginFrame(IRMath::vec3 cameraWorldVoxel) {
    ++m_frameIndex;
    m_cameraWorldVoxel = cameraWorldVoxel;
    // Floor before cast — truncation toward zero misclassifies negative fractional
    // positions (e.g. -0.5 → 0 instead of -1). worldToChunk's floor-divide handles
    // integer voxels correctly; this seam owns the float→int step.
    m_cameraChunk = IRPrefab::Chunk::worldToChunk(IRMath::ivec3(IRMath::floor(cameraWorldVoxel)));
    m_frameStats = {};

    const float evictThreshold = m_config.prefetchRadiusVoxels_ + m_config.hysteresisVoxels_;

    for (auto &kv : m_slots) {
        ChunkResidencySlot &s = kv.second;
        if (s.state_ == ChunkResidencyState::EVICTING) {
            continue;
        }

        auto center = IRPrefab::Chunk::chunkCenterWorld(IRPrefab::Chunk::unpack(kv.first));
        s.distanceVoxels_ = IRMath::length(cameraWorldVoxel - center);

        if (s.distanceVoxels_ <= m_config.viewRadiusVoxels_) {
            s.lastTouchedFrame_ = m_frameIndex;
        }

        if (s.distanceVoxels_ > evictThreshold && s.state_ == ChunkResidencyState::RESIDENT) {
            s.state_ = ChunkResidencyState::EVICTING;
        }
    }
}

void ChunkResidencyManager::tickPrefetch() {
    IR_PROFILE_SCOPE("ChunkResidencyManager::tickPrefetch");
    const int r = m_config.prefetchRadiusChunks_;
    if (r <= 0) {
        return;
    }

    for (int dx = -r; dx <= r; ++dx) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dz = -r; dz <= r; ++dz) {
                const IRMath::ivec3 coord{
                    m_cameraChunk.x + dx,
                    m_cameraChunk.y + dy,
                    m_cameraChunk.z + dz
                };
                const auto key = IRPrefab::Chunk::pack(coord);
                const RequestPriority prio = (dx == 0 && dy == 0 && dz == 0)
                                                 ? RequestPriority::VISIBLE_RENDER
                                                 : RequestPriority::PREFETCH_RING;
                requestResident(key, prio);

                if (auto *s = slot(key)) {
                    s->distanceVoxels_ =
                        IRMath::length(IRPrefab::Chunk::chunkCenterWorld(coord) - m_cameraWorldVoxel);
                }
            }
        }
    }
    // Eviction is driven by beginFrame() (Euclidean distance + hysteresis) and
    // processed by endFrame() — tickPrefetch only grows the resident set.
}

void ChunkResidencyManager::flushUploads(int maxBytes) {
    if (!m_config.deferredUpload_ || m_pendingUploads.empty()) {
        return;
    }

    IR_PROFILE_SCOPE("ChunkResidencyManager::flushUploads");

    // Resolve the per-call budget. maxBytes == 0 → use Config default.
    // Negative budgets are clamped to 0 (defaults) to avoid silent
    // budget-bypass — a caller passing -1 expecting "unlimited" would
    // silently disable the cap on every frame they did so.
    int effectiveBudget = maxBytes > 0 ? maxBytes : m_config.defaultUploadBudgetBytes_;
    if (effectiveBudget < 0) {
        effectiveBudget = 0;
    }

    // Sort by (urgency, distance). Stable sort keeps enqueue order as the
    // tiebreaker for chunks at the same urgency + distance — preserves the
    // intuitive "first requested wins" for same-class peers.
    std::stable_sort(
        m_pendingUploads.begin(),
        m_pendingUploads.end(),
        [this](const PendingUpload &a, const PendingUpload &b) {
            const int ua = requestPriorityUrgency(a.priority_);
            const int ub = requestPriorityUrgency(b.priority_);
            if (ua != ub) {
                return ua < ub;
            }
            auto ia = m_slots.find(a.key_);
            auto ib = m_slots.find(b.key_);
            const float da = ia != m_slots.end() ? ia->second.distanceVoxels_ : 0.0f;
            const float db = ib != m_slots.end() ? ib->second.distanceVoxels_ : 0.0f;
            return da < db;
        }
    );

    const std::uint64_t bytesPerChunk = static_cast<std::uint64_t>(m_config.voxelsPerChunk_) *
                                        static_cast<std::uint64_t>(sizeof(IRComponents::C_Voxel));

    std::uint64_t cumulativeBytes = 0;
    m_deferScratch.clear();

    for (const auto &pending : m_pendingUploads) {
        // Slot may have been evicted between request and flush — the
        // pending entry then references a chunk that no longer has a
        // slot. Drop it silently.
        auto it = m_slots.find(pending.key_);
        if (it == m_slots.end()) {
            continue;
        }
        ChunkResidencySlot &s = it->second;
        // A re-request can land while a chunk is already RESIDENT (sync
        // mode never reaches this point, but a slot can become RESIDENT
        // from a forced bypass earlier in this same call). Drop the
        // entry — work already done.
        if (s.state_ == ChunkResidencyState::RESIDENT) {
            continue;
        }
        if (s.state_ == ChunkResidencyState::EVICTING) {
            // Eviction is queued; don't fight the eviction by re-loading
            // mid-flight. Drop the upload request.
            continue;
        }

        const bool isForced = pending.priority_ == RequestPriority::FORCED;
        const bool wouldExceed =
            bytesPerChunk > 0 &&
            cumulativeBytes + bytesPerChunk > static_cast<std::uint64_t>(effectiveBudget);

        // FORCED bypasses the budget (editor "load this chunk now").
        // For non-forced: budget cuts off the drain, BUT we always
        // process at least one chunk per call even when a single chunk
        // exceeds the budget. Otherwise streaming stalls forever the
        // moment a chunk grows past the cap.
        if (wouldExceed && cumulativeBytes > 0 && !isForced) {
            m_deferScratch.push_back(pending);
            continue;
        }

        completeUploadForSlot(s);
        cumulativeBytes += bytesPerChunk;
    }

    std::swap(m_pendingUploads, m_deferScratch);
}

void ChunkResidencyManager::endFrame() {
    IR_PROFILE_FUNCTION(profiler::colors::Blue500);

    // 1. Process EVICTING slots: save dirty, deallocate pool, erase.
    {
        auto it = m_slots.begin();
        while (it != m_slots.end()) {
            if (it->second.state_ == ChunkResidencyState::EVICTING) {
                evictSlot(it);
                it = m_slots.erase(it);
                ++m_frameStats.evictedThisFrame_;
            } else {
                ++it;
            }
        }
    }

    // 2. Enforce budget: if still over maxResidentChunks_, evict
    //    furthest-from-camera slots with LRU tie-breaking.
    if (m_slots.size() > m_config.maxResidentChunks_) {
        struct EvictCandidate {
            IRPrefab::Chunk::ChunkKey key_;
            float distance_;
            std::uint64_t lastTouched_;
        };
        std::vector<EvictCandidate> candidates;
        candidates.reserve(m_slots.size());
        for (const auto &kv : m_slots) {
            candidates.push_back({kv.first, kv.second.distanceVoxels_, kv.second.lastTouchedFrame_}
            );
        }
        std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
            if (a.distance_ != b.distance_)
                return a.distance_ > b.distance_;
            return a.lastTouched_ < b.lastTouched_;
        });

        auto toEvict = static_cast<std::size_t>(m_slots.size() - m_config.maxResidentChunks_);
        for (std::size_t i = 0; i < toEvict && i < candidates.size(); ++i) {
            auto it = m_slots.find(candidates[i].key_);
            if (it != m_slots.end()) {
                evictSlot(it);
                m_slots.erase(it);
                ++m_frameStats.evictedThisFrame_;
            }
        }
    }

    m_frameStats.residentCount_ = static_cast<unsigned int>(m_slots.size());
}

bool ChunkResidencyManager::isResident(IRPrefab::Chunk::ChunkKey key) const {
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return false;
    }
    return it->second.state_ == ChunkResidencyState::RESIDENT;
}

const ChunkResidencySlot *ChunkResidencyManager::slot(IRPrefab::Chunk::ChunkKey key) const {
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return nullptr;
    }
    return &it->second;
}

ChunkResidencySlot *ChunkResidencyManager::slot(IRPrefab::Chunk::ChunkKey key) {
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return nullptr;
    }
    return &it->second;
}

void ChunkResidencyManager::requestResident(
    IRPrefab::Chunk::ChunkKey key, RequestPriority priority
) {
    auto [it, inserted] = m_slots.try_emplace(key);
    ChunkResidencySlot &s = it->second;
    s.lastTouchedFrame_ = m_frameIndex;
    if (!inserted) {
        // Rescue a slot scheduled for eviction — it was touched again before
        // endFrame could erase it. Clearing EVICTING here is the only safe
        // place: callers of migrateEntity and attachEntity rely on
        // requestResident having done this before they write to the slot.
        if (s.state_ == ChunkResidencyState::EVICTING) {
            s.state_ = ChunkResidencyState::RESIDENT;
        }
        // In deferred mode, a re-request can bump the queued entry's priority
        // (e.g. PREFETCH_RING → VISIBLE_RENDER when the camera reaches the
        // chunk) so flushUploads drains it sooner. RESIDENT slots (including
        // freshly-rescued EVICTING slots above) ignore the bump.
        if (m_config.deferredUpload_ && s.state_ != ChunkResidencyState::RESIDENT) {
            bumpPendingUploadPriority(key, priority);
        }
        return;
    }

    s.key_ = key;
    s.state_ = ChunkResidencyState::LOADING;

    if (m_config.deferredUpload_) {
        // Async / budget mode: enqueue and let flushUploads complete it.
        // Slot stays in LOADING; renderer paints it as a low-LOD AABB
        // billboard from the slot's `aabbColor_` / `aabbMinVoxel_` /
        // `aabbMaxVoxel_` defaults until the upload lands.
        m_pendingUploads.push_back(PendingUpload{key, priority, m_frameIndex});
        return;
    }

    // E1 synchronous path: complete the upload inline.
    completeUploadForSlot(s);
}

void ChunkResidencyManager::completeUploadForSlot(ChunkResidencySlot &s) {
    // Caller has just inserted the slot (sync path) or is draining the
    // pending-uploads queue (async path). In both cases the slot is in
    // LOADING and we drive it through UPLOADING → RESIDENT.
    if (m_config.poolAllocator_ && m_config.voxelsPerChunk_ > 0) {
        std::optional<std::vector<IRAsset::VoxelRecord>> loaded;
        if (m_config.persistence_) {
            loaded = m_config.persistence_->loadChunk(s.key_);
        }

        s.state_ = ChunkResidencyState::UPLOADING;
        s.poolAllocation_ = m_config.poolAllocator_(m_config.voxelsPerChunk_);

        if (loaded && s.poolAllocation_.voxels_.size() == loaded->size()) {
            seedPoolSliceFromRecords(s.poolAllocation_, *loaded);
        } else if (loaded) {
            IRE_LOG_WARN(
                "ChunkResidencyManager::completeUploadForSlot: loaded record count {} != pool "
                "slice size {}; ignoring disk data",
                loaded->size(),
                s.poolAllocation_.voxels_.size()
            );
        }
    }
    s.state_ = ChunkResidencyState::RESIDENT;
    // m_dirty stays false — disk and memory agree after a fresh load /
    // alloc. Mutators are responsible for routing through markChunkDirty.
    ++m_frameStats.loadedThisFrame_;
}

void ChunkResidencyManager::evictSlot(
    std::unordered_map<IRPrefab::Chunk::ChunkKey, ChunkResidencySlot>::iterator it
) {
    ChunkResidencySlot &s = it->second;

    if (s.m_dirty && m_config.persistence_ && !s.poolAllocation_.voxels_.empty()) {
        auto records = poolSliceToRecords(s.poolAllocation_);
        auto status = m_config.persistence_->saveChunk(it->first, records);
        if (!status.ok()) {
            IRE_LOG_ERROR(
                "ChunkResidencyManager::evictSlot: save failed for chunk (code={}): {}",
                static_cast<int>(status.code_),
                status.message_
            );
        }
    }

    if (m_config.poolDeallocator_ && !s.poolAllocation_.voxels_.empty()) {
        m_config.poolDeallocator_(s.poolAllocation_);
    }
}

void ChunkResidencyManager::bumpPendingUploadPriority(
    IRPrefab::Chunk::ChunkKey key, RequestPriority p
) {
    for (auto &pending : m_pendingUploads) {
        if (pending.key_ != key) {
            continue;
        }
        if (requestPriorityUrgency(p) < requestPriorityUrgency(pending.priority_)) {
            pending.priority_ = p;
        }
        return;
    }
}

void ChunkResidencyManager::requestEvict(IRPrefab::Chunk::ChunkKey key) {
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return;
    }
    evictSlot(it);
    m_slots.erase(it);

    // Drop any pending-upload entries that referenced this key — they
    // would otherwise resurface as zombie work on the next flushUploads.
    // flushUploads also guards (slot-missing → skip) so this is
    // defense in depth, but eagerly trimming keeps the queue short.
    if (!m_pendingUploads.empty()) {
        m_pendingUploads.erase(
            std::remove_if(
                m_pendingUploads.begin(),
                m_pendingUploads.end(),
                [key](const PendingUpload &p) { return p.key_ == key; }
            ),
            m_pendingUploads.end()
        );
    }
}

void ChunkResidencyManager::flushPendingSaves() {
    if (!m_config.persistence_) {
        return;
    }
    for (auto &kv : m_slots) {
        ChunkResidencySlot &s = kv.second;
        if (!s.m_dirty || s.poolAllocation_.voxels_.empty()) {
            continue;
        }
        auto records = poolSliceToRecords(s.poolAllocation_);
        auto status = m_config.persistence_->saveChunk(kv.first, records);
        if (status.ok()) {
            s.m_dirty = false;
        } else {
            IRE_LOG_ERROR(
                "ChunkResidencyManager::flushPendingSaves: save failed for chunk (code={}): {}",
                static_cast<int>(status.code_),
                status.message_
            );
        }
    }
}

void ChunkResidencyManager::markChunkDirty(IRPrefab::Chunk::ChunkKey key) {
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return;
    }
    it->second.m_dirty = true;
}

void ChunkResidencyManager::attachEntity(IREntity::EntityId id, IRPrefab::Chunk::ChunkKey key) {
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return;
    }
    if (it->second.state_ == ChunkResidencyState::EVICTING) {
        it->second.state_ = ChunkResidencyState::RESIDENT;
    }
    auto &owned = it->second.ownedEntities_;
    if (std::find(owned.begin(), owned.end(), id) == owned.end()) {
        owned.push_back(id);
        markChunkDirty(key);
    }
}

void ChunkResidencyManager::migrateEntity(
    IREntity::EntityId id, IRPrefab::Chunk::ChunkKey oldKey, IRPrefab::Chunk::ChunkKey newKey
) {
    if (oldKey == newKey) {
        return;
    }

    if (auto src = m_slots.find(oldKey); src != m_slots.end()) {
        auto &owned = src->second.ownedEntities_;
        auto newEnd = std::remove(owned.begin(), owned.end(), id);
        if (newEnd != owned.end()) {
            owned.erase(newEnd, owned.end());
            markChunkDirty(oldKey);
        }
    }

    if (!isResident(newKey)) {
        // Destination not yet resident (or is EVICTING) — force it in. The
        // design's pending-migration queue (deferred until destination upload
        // settles) is an E4 concern.
        requestResident(newKey, RequestPriority::FORCED);
    }
    attachEntity(id, newKey);
}

std::size_t ChunkResidencyManager::residentChunkCount() const {
    return m_slots.size();
}

std::size_t ChunkResidencyManager::pendingUploadCount() const {
    return m_pendingUploads.size();
}

std::size_t ChunkResidencyManager::entityCount() const {
    std::size_t total = 0;
    for (const auto &kv : m_slots) {
        total += kv.second.ownedEntities_.size();
    }
    return total;
}

} // namespace IRWorld
