#ifndef SYSTEM_GRID_BAKE_H
#define SYSTEM_GRID_BAKE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/math/nav_types.hpp>
#include <irreden/update/components/component_nav_cell.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>
#include <irreden/update/components/component_chunk_coord.hpp>
#include <irreden/update/components/component_flow_field.hpp>
#include <irreden/update/components/component_flow_field_request.hpp>
#include <irreden/update/components/component_nav_chunk_data.hpp>

#include <unordered_set>
#include <cmath>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<GRID_BAKE> {
    static SystemId create() {
        return createSystem<C_NavWorld, C_ChunkRegistry>(
            "GridBake",
            [](IREntity::EntityId levelEntity, C_NavWorld &navWorld, C_ChunkRegistry &registry) {
                if (!navWorld.dirty_) return;

                registry.clearChunks();

                // Collect all cell positions and their passability for clearance computation
                std::unordered_map<int64_t, bool> cellPassable;

                auto cellNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_NavCell>()
                );

                for (auto *node : cellNodes) {
                    auto &cells = IREntity::getComponentData<C_NavCell>(node);
                    for (auto &cell : cells) {
                        ChunkCoord chunkCoord = worldCellToChunkCoord(cell.gridPos_, navWorld.chunkSize_);
                        IREntity::EntityId chunkEntityId = registry.getChunk(chunkCoord);

                        if (chunkEntityId == IREntity::kNullEntity) {
                            vec3 chunkWorldOrigin = navWorld.origin_ + vec3(
                                static_cast<float>(chunkCoord.x * navWorld.chunkSize_.x) * navWorld.cellSizeWorld_,
                                static_cast<float>(chunkCoord.y * navWorld.chunkSize_.y) * navWorld.cellSizeWorld_,
                                0.0f
                            );
                            chunkEntityId = IREntity::createEntity(
                                C_ChunkCoord(chunkCoord),
                                C_NavChunkData(navWorld.chunkSize_, chunkWorldOrigin, navWorld.cellSizeWorld_)
                            );
                            registry.setChunk(chunkCoord, chunkEntityId, nullptr);
                        }

                        auto &chunkData = IREntity::getComponent<C_NavChunkData>(chunkEntityId);
                        ivec3 localPos = worldCellToLocalCell(cell.gridPos_, chunkCoord, navWorld.chunkSize_);
                        float height = static_cast<float>(cell.gridPos_.z);
                        chunkData.addCell(localPos, cell.passable_, 0.0f, height);

                        cellPassable[posToKey(cell.gridPos_)] = cell.passable_;
                    }
                }

                auto chunkNodes = IREntity::queryArchetypeNodesSimple(
                    IREntity::getArchetype<C_ChunkCoord, C_NavChunkData>()
                );
                for (auto *node : chunkNodes) {
                    auto &coords = IREntity::getComponentData<C_ChunkCoord>(node);
                    auto &chunkDatas = IREntity::getComponentData<C_NavChunkData>(node);
                    for (size_t i = 0; i < chunkDatas.size(); i++) {
                        registry.setChunk(coords[i].coord_, node->entities_[i], &chunkDatas[i]);
                    }
                }

                // Compute true clearance: for each passable cell, find the
                // Chebyshev distance (in cells) to the nearest wall.
                // Max search radius covers the largest useful agent size.
                float maxAgentWorld = navWorld.getDefaultAgentClearanceWorld() * 2.0f;
                int maxRadius = static_cast<int>(std::ceil(maxAgentWorld / navWorld.cellSizeWorld_)) + 1;
                maxRadius = std::min(maxRadius, 8);

                for (auto *node : cellNodes) {
                    auto &cells = IREntity::getComponentData<C_NavCell>(node);
                    for (auto &cell : cells) {
                        if (!cell.passable_) continue;
                        if (cell.clearance_ > 0.0f) continue;

                        float minDistCells = static_cast<float>(maxRadius + 1);
                        for (int dx = -maxRadius; dx <= maxRadius; dx++) {
                            for (int dy = -maxRadius; dy <= maxRadius; dy++) {
                                if (dx == 0 && dy == 0) continue;
                                ivec3 neighbor = cell.gridPos_ + ivec3(dx, dy, 0);
                                int64_t nkey = posToKey(neighbor);
                                auto it = cellPassable.find(nkey);
                                bool isWall = (it == cellPassable.end()) || !it->second;
                                if (isWall) {
                                    float dist = std::sqrt(
                                        static_cast<float>(dx * dx + dy * dy));
                                    if (dist < minDistCells) minDistCells = dist;
                                }
                            }
                        }

                        // Clearance in world units: distance in cells * cellSize,
                        // minus half a cell (wall extends to cell edge)
                        float clearanceWorld = (minDistCells - 0.5f) * navWorld.cellSizeWorld_;
                        if (clearanceWorld < 0.0f) clearanceWorld = 0.0f;

                        ChunkCoord chunkCoord = worldCellToChunkCoord(cell.gridPos_, navWorld.chunkSize_);
                        auto *chunkData = registry.getChunkData(chunkCoord);
                        if (!chunkData) continue;
                        ivec3 localPos = worldCellToLocalCell(cell.gridPos_, chunkCoord, navWorld.chunkSize_);
                        int idx = chunkData->getLocalIndex(localPos);
                        if (idx >= 0) {
                            chunkData->clearance_[static_cast<size_t>(idx)] = clearanceWorld;
                        }
                    }
                }

                if (auto requests = IREntity::getComponentOptional<C_FlowFieldRequest>(levelEntity)) {
                    for (auto &request : (*requests)->requests_) {
                        request.dirty_ = true;
                    }
                }
                if (auto flowField = IREntity::getComponentOptional<C_FlowField>(levelEntity)) {
                    for (auto &field : (*flowField)->fields_) {
                        field.dirty_ = true;
                        field.complete_ = false;
                        field.frontier_ = {};
                        field.chunks_.clear();
                    }
                }

                navWorld.dirty_ = false;
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GRID_BAKE_H */
