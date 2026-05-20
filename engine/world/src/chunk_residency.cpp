#include <irreden/world/chunk_residency.hpp>

#include <algorithm>
#include <utility>

namespace IRWorld {

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
        s.state_ = ChunkResidencyState::UPLOADING;
        s.poolAllocation_ = m_config.poolAllocator_(m_config.voxelsPerChunk_);
    }
    s.state_ = ChunkResidencyState::RESIDENT;
}

void ChunkResidencyManager::requestEvict(IRPrefab::Chunk::ChunkKey key) {
    auto it = m_slots.find(key);
    if (it == m_slots.end()) {
        return;
    }
    // E2 introduces a real EVICTING phase + async save; today eviction
    // is synchronous and drops the slot. The pool slice is leaked back
    // to the global pool — today's allocator is bump-style (see
    // engine/render/CLAUDE.md "Voxel pool allocation").
    m_slots.erase(it);
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
