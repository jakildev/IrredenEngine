#ifndef COMPONENT_CHUNK_REGISTRY_H
#define COMPONENT_CHUNK_REGISTRY_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/math/nav_types.hpp>
#include <irreden/update/components/component_nav_chunk_data.hpp>

#include <unordered_map>

namespace IRComponents {

struct C_ChunkRegistry {
    struct ChunkEntry {
        IREntity::EntityId entityId_{IREntity::kNullEntity};
        C_NavChunkData *data_{nullptr};
    };

    std::unordered_map<int64_t, ChunkEntry> coordToChunk_;

    void setChunk(IRMath::ChunkCoord coord, IREntity::EntityId chunkEntityId, C_NavChunkData *data) {
        coordToChunk_[IRMath::chunkCoordToKey(coord)] = ChunkEntry{chunkEntityId, data};
    }

    IREntity::EntityId getChunk(IRMath::ChunkCoord coord) const {
        auto it = coordToChunk_.find(IRMath::chunkCoordToKey(coord));
        return (it != coordToChunk_.end()) ? it->second.entityId_ : IREntity::kNullEntity;
    }

    C_NavChunkData *getChunkData(IRMath::ChunkCoord coord) const {
        auto it = coordToChunk_.find(IRMath::chunkCoordToKey(coord));
        return (it != coordToChunk_.end()) ? it->second.data_ : nullptr;
    }

    bool hasChunk(IRMath::ChunkCoord coord) const {
        return coordToChunk_.find(IRMath::chunkCoordToKey(coord)) != coordToChunk_.end();
    }

    void clearChunks() {
        for (auto &kv : coordToChunk_) {
            if (kv.second.entityId_ != IREntity::kNullEntity) {
                IREntity::destroyEntity(kv.second.entityId_);
            }
        }
        coordToChunk_.clear();
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CHUNK_REGISTRY_H */
