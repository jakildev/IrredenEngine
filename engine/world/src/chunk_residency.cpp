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

void ChunkResidencyManager::beginFrame() {
    ++m_frameIndex;
}

void ChunkResidencyManager::tickPrefetch() {}

void ChunkResidencyManager::flushUploads(int) {}

void ChunkResidencyManager::endFrame() {}

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
    IRPrefab::Chunk::ChunkKey key, RequestPriority /*priority*/
) {
    auto [it, inserted] = m_slots.try_emplace(key);
    ChunkResidencySlot &s = it->second;
    s.lastTouchedFrame_ = m_frameIndex;
    if (!inserted) {
        // Already in the resident set — just refresh the touch frame.
        return;
    }

    s.key_ = key;
    s.state_ = ChunkResidencyState::LOADING;

    // E1 skeleton: synchronous allocate + transition to RESIDENT. The
    // async load/upload pipeline lands in E3.
    if (m_config.poolAllocator_ && m_config.voxelsPerChunk_ > 0) {
        std::optional<std::vector<IRAsset::VoxelRecord>> loaded;
        if (m_config.persistence_) {
            loaded = m_config.persistence_->loadChunk(key);
        }

        s.state_ = ChunkResidencyState::UPLOADING;
        s.poolAllocation_ = m_config.poolAllocator_(m_config.voxelsPerChunk_);

        if (loaded && s.poolAllocation_.voxels_.size() == loaded->size()) {
            seedPoolSliceFromRecords(s.poolAllocation_, *loaded);
        } else if (loaded) {
            IRE_LOG_WARN(
                "ChunkResidencyManager::requestResident: loaded record count {} != pool slice "
                "size {}; ignoring disk data",
                loaded->size(),
                s.poolAllocation_.voxels_.size()
            );
        }
    }
    s.state_ = ChunkResidencyState::RESIDENT;
    // dirty_ stays false — disk and memory agree after a fresh load /
    // alloc. Mutators are responsible for flipping it.
}

void ChunkResidencyManager::requestEvict(IRPrefab::Chunk::ChunkKey key) {
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return;
    }

    ChunkResidencySlot &s = it->second;
    if (s.dirty_ && m_config.persistence_ && !s.poolAllocation_.voxels_.empty()) {
        // Synchronous save before erase — the "snapshot-at-schedule-time"
        // safety the design calls for is implicit here because we save
        // and erase in the same blocking call; nothing can mutate the
        // slice between the two. E2/E3 lift this into the worker pool.
        auto records = poolSliceToRecords(s.poolAllocation_);
        auto status = m_config.persistence_->saveChunk(key, records);
        if (!status.ok()) {
            IRE_LOG_ERROR(
                "ChunkResidencyManager::requestEvict: save failed for chunk (code={}): {}",
                static_cast<int>(status.code_),
                status.message_
            );
        }
    }

    // E2 introduces a real EVICTING phase + async save; today eviction
    // is synchronous and drops the slot. The pool slice is leaked back
    // to the global pool — today's allocator is bump-style (see
    // engine/render/CLAUDE.md "Voxel pool allocation").
    m_slots.erase(it);
}

void ChunkResidencyManager::flushPendingSaves() {
    if (!m_config.persistence_) {
        return;
    }
    for (auto &kv : m_slots) {
        ChunkResidencySlot &s = kv.second;
        if (!s.dirty_ || s.poolAllocation_.voxels_.empty()) {
            continue;
        }
        auto records = poolSliceToRecords(s.poolAllocation_);
        auto status = m_config.persistence_->saveChunk(kv.first, records);
        if (status.ok()) {
            s.dirty_ = false;
        } else {
            IRE_LOG_ERROR(
                "ChunkResidencyManager::flushPendingSaves: save failed for chunk (code={}): {}",
                static_cast<int>(status.code_),
                status.message_
            );
        }
    }
}

void ChunkResidencyManager::attachEntity(IREntity::EntityId id, IRPrefab::Chunk::ChunkKey key) {
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return;
    }
    auto &owned = it->second.ownedEntities_;
    if (std::find(owned.begin(), owned.end(), id) == owned.end()) {
        owned.push_back(id);
        it->second.dirty_ = true;
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
            src->second.dirty_ = true;
        }
    }

    if (m_slots.find(newKey) == m_slots.end()) {
        // Destination not yet resident — force it in. The design's
        // pending-migration queue (deferred until destination upload
        // settles) is an E4 concern.
        requestResident(newKey, RequestPriority::FORCED);
    }
    attachEntity(id, newKey);
}

std::size_t ChunkResidencyManager::residentChunkCount() const {
    return m_slots.size();
}

std::size_t ChunkResidencyManager::entityCount() const {
    std::size_t total = 0;
    for (const auto &kv : m_slots) {
        total += kv.second.ownedEntities_.size();
    }
    return total;
}

} // namespace IRWorld
