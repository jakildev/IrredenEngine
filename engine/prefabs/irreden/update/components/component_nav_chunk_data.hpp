#ifndef COMPONENT_NAV_CHUNK_DATA_H
#define COMPONENT_NAV_CHUNK_DATA_H

#include <irreden/ir_math.hpp>
#include <irreden/math/nav_types.hpp>

#include <vector>

namespace IRComponents {

struct C_NavChunkData {
    IRMath::ivec3 chunkSize_;
    IRMath::vec3 worldOrigin_;
    float cellSizeWorld_;

    std::vector<bool> exists_;
    std::vector<bool> walkable_;
    std::vector<float> clearance_;
    std::vector<float> height_;

    C_NavChunkData()
        : chunkSize_{0, 0, 0}
        , worldOrigin_{0.0f}
        , cellSizeWorld_{1.0f} {}

    C_NavChunkData(IRMath::ivec3 chunkSize, IRMath::vec3 worldOrigin, float cellSizeWorld = 1.0f)
        : chunkSize_{chunkSize}
        , worldOrigin_{worldOrigin}
        , cellSizeWorld_{cellSizeWorld} {
        int totalCells = chunkSize_.x * chunkSize_.y * chunkSize_.z;
        exists_.resize(static_cast<size_t>(totalCells), false);
        walkable_.resize(static_cast<size_t>(totalCells), false);
        clearance_.resize(static_cast<size_t>(totalCells), 0.0f);
        height_.resize(static_cast<size_t>(totalCells), 0.0f);
    }

    int localPosToIndex(IRMath::ivec3 localPos) const {
        if (localPos.x < 0 || localPos.x >= chunkSize_.x ||
            localPos.y < 0 || localPos.y >= chunkSize_.y ||
            localPos.z < 0 || localPos.z >= chunkSize_.z) {
            return -1;
        }
        return localPos.x + localPos.y * chunkSize_.x + localPos.z * chunkSize_.x * chunkSize_.y;
    }

    int addCell(IRMath::ivec3 localPos, bool passable, float clearance = 1.0f, float height = 0.0f) {
        int index = localPosToIndex(localPos);
        if (index < 0) return -1;

        size_t idx = static_cast<size_t>(index);
        exists_[idx] = true;
        walkable_[idx] = passable;
        clearance_[idx] = clearance;
        height_[idx] = height;
        return index;
    }

    bool hasCell(IRMath::ivec3 localPos) const {
        int index = localPosToIndex(localPos);
        return index >= 0 && exists_[static_cast<size_t>(index)];
    }

    int getLocalIndex(IRMath::ivec3 localPos) const {
        int index = localPosToIndex(localPos);
        if (index < 0) return -1;
        return exists_[static_cast<size_t>(index)] ? index : -1;
    }

    float getClearance(int localIdx) const {
        return (localIdx >= 0 && localIdx < static_cast<int>(clearance_.size())) ? clearance_[static_cast<size_t>(localIdx)] : 0.0f;
    }

    bool isPassable(int localIdx, float agentClearance) const {
        if (localIdx < 0 || localIdx >= static_cast<int>(walkable_.size())) return false;
        return exists_[static_cast<size_t>(localIdx)] &&
               walkable_[static_cast<size_t>(localIdx)] &&
               clearance_[static_cast<size_t>(localIdx)] >= agentClearance;
    }

    bool isPassableFast(int localIdx, float agentClearance) const {
        return exists_[static_cast<size_t>(localIdx)] &&
               walkable_[static_cast<size_t>(localIdx)] &&
               clearance_[static_cast<size_t>(localIdx)] >= agentClearance;
    }

    IRMath::vec3 localCellToWorld(IRMath::ivec3 localPos) const {
        return worldOrigin_ + IRMath::vec3(
            static_cast<float>(localPos.x) * cellSizeWorld_,
            static_cast<float>(localPos.y) * cellSizeWorld_,
            -static_cast<float>(localPos.z) * cellSizeWorld_
        );
    }
};

} // namespace IRComponents

#endif /* COMPONENT_NAV_CHUNK_DATA_H */
