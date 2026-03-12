#ifndef NAV_QUERY_H
#define NAV_QUERY_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/math/nav_types.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>
#include <irreden/update/components/component_chunk_coord.hpp>
#include <irreden/update/components/component_nav_chunk_data.hpp>
#include <irreden/update/components/component_nav_cell.hpp>
#include <irreden/common/components/component_position_3d.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

namespace IRMath {

namespace {

inline int64_t worldCellToKey(ivec3 c) {
    return posToKey(c);
}

// Octile heuristic: admissible for 8/18-connected grids with diagonal cost sqrt(2)
inline float heuristicChunked(ivec3 a, ivec3 b) {
    float dx = static_cast<float>(a.x > b.x ? a.x - b.x : b.x - a.x);
    float dy = static_cast<float>(a.y > b.y ? a.y - b.y : b.y - a.y);
    float dz = static_cast<float>(a.z > b.z ? a.z - b.z : b.z - a.z);
    constexpr float kSqrt2 = 1.41421356f;
    float minXY = (dx < dy) ? dx : dy;
    float maxXY = (dx > dy) ? dx : dy;
    return (maxXY - minXY) + kSqrt2 * minXY + dz;
}

inline float moveCostForDelta(ivec3 delta) {
    int ax = delta.x < 0 ? -delta.x : delta.x;
    int ay = delta.y < 0 ? -delta.y : delta.y;
    int az = delta.z < 0 ? -delta.z : delta.z;
    int nonZero = (ax > 0 ? 1 : 0) + (ay > 0 ? 1 : 0) + (az > 0 ? 1 : 0);
    if (nonZero >= 3) return 1.73205080f; // sqrt(3)
    if (nonZero == 2) return 1.41421356f; // sqrt(2)
    return 1.0f;
}

inline const IRComponents::C_NavChunkData *getChunkDataForWorldCell(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    ivec3 worldCell,
    ChunkCoord &chunkCoord,
    ivec3 &localCell
) {
    chunkCoord = worldCellToChunkCoord(worldCell, navWorld.chunkSize_);
    const auto *chunkData = registry.getChunkData(chunkCoord);
    if (!chunkData) return nullptr;
    localCell = worldCellToLocalCell(worldCell, chunkCoord, navWorld.chunkSize_);
    return chunkData;
}

} // namespace

// ---- Reference-based overloads (no getComponent per call) ----

inline ivec3 navWorldToCell(
    const IRComponents::C_NavWorld &navWorld,
    vec3 world
) {
    vec3 scaled = (world - navWorld.origin_) / navWorld.cellSizeWorld_;
    return ivec3(IRMath::floor(scaled.x), IRMath::floor(scaled.y), IRMath::floor(scaled.z));
}

inline vec3 navCellToWorld(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    ivec3 worldCell
) {
    ChunkCoord chunkCoord;
    ivec3 localCell;
    const auto *chunkData = getChunkDataForWorldCell(
        navWorld, registry, worldCell, chunkCoord, localCell
    );
    if (!chunkData) {
        return navWorld.origin_ + vec3(
            static_cast<float>(worldCell.x) * navWorld.cellSizeWorld_,
            static_cast<float>(worldCell.y) * navWorld.cellSizeWorld_,
            -static_cast<float>(worldCell.z) * navWorld.cellSizeWorld_
        );
    }
    return chunkData->localCellToWorld(localCell);
}

inline bool navCellExists(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    ivec3 worldCell
) {
    ChunkCoord chunkCoord;
    ivec3 localCell;
    const auto *chunkData = getChunkDataForWorldCell(
        navWorld, registry, worldCell, chunkCoord, localCell
    );
    return chunkData && chunkData->hasCell(localCell);
}

inline bool navIsPassable(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    ivec3 worldCell,
    float agentClearance = 0.5f
) {
    ChunkCoord chunkCoord;
    ivec3 localCell;
    const auto *chunkData = getChunkDataForWorldCell(
        navWorld, registry, worldCell, chunkCoord, localCell
    );
    if (!chunkData) return false;
    int localIdx = chunkData->getLocalIndex(localCell);
    return localIdx >= 0 && chunkData->isPassableFast(localIdx, agentClearance);
}

inline float navGetCellClearance(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    ivec3 worldCell
) {
    ChunkCoord chunkCoord;
    ivec3 localCell;
    const auto *chunkData = getChunkDataForWorldCell(
        navWorld, registry, worldCell, chunkCoord, localCell
    );
    if (!chunkData) return 0.0f;
    int localIdx = chunkData->getLocalIndex(localCell);
    if (localIdx < 0) return 0.0f;
    return chunkData->getClearance(localIdx);
}

inline float navGetClearanceAtWorld(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    float worldX,
    float worldY,
    float worldZ = 0.0f
) {
    ivec3 worldCell = navWorldToCell(navWorld, vec3(worldX, worldY, worldZ));
    return navGetCellClearance(navWorld, registry, worldCell);
}

template <typename Function>
inline void navForEachPassableNeighbor(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    ivec3 worldCell,
    float agentClearance,
    Function &&function
) {
    static const ivec3 neighborDeltas[] = {
        {1, 0, 0},  {-1, 0, 0},  {0, 1, 0},  {0, -1, 0},
        {0, 0, 1},  {0, 0, -1},
        {1, 1, 0},  {1, -1, 0},  {-1, 1, 0},  {-1, -1, 0},
        {1, 0, 1},  {1, 0, -1},  {-1, 0, 1},  {-1, 0, -1},
        {0, 1, 1},  {0, 1, -1},   {0, -1, 1},  {0, -1, -1}
    };
    ChunkCoord lastChunkCoord(0, 0);
    const IRComponents::C_NavChunkData *lastChunkData = nullptr;
    bool hasLastChunk = false;

    for (const auto &delta : neighborDeltas) {
        ivec3 neighborWorldCell = worldCell + delta;
        ChunkCoord neighborChunkCoord = worldCellToChunkCoord(neighborWorldCell, navWorld.chunkSize_);
        const IRComponents::C_NavChunkData *neighborChunkData = nullptr;
        if (hasLastChunk &&
            neighborChunkCoord.x == lastChunkCoord.x &&
            neighborChunkCoord.y == lastChunkCoord.y) {
            neighborChunkData = lastChunkData;
        } else {
            neighborChunkData = registry.getChunkData(neighborChunkCoord);
            lastChunkCoord = neighborChunkCoord;
            lastChunkData = neighborChunkData;
            hasLastChunk = true;
        }
        if (!neighborChunkData) continue;
        ivec3 localNeighbor = worldCellToLocalCell(neighborWorldCell, neighborChunkCoord, navWorld.chunkSize_);
        int localIdx = neighborChunkData->getLocalIndex(localNeighbor);
        if (localIdx < 0 || !neighborChunkData->isPassableFast(localIdx, agentClearance)) continue;
        function(neighborWorldCell);
    }
}

inline std::vector<ivec3> navGetNeighborCells(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    ivec3 worldCell
) {
    std::vector<ivec3> neighbors;
    neighbors.reserve(18);
    navForEachPassableNeighbor(
        navWorld,
        registry,
        worldCell,
        0.0f,
        [&neighbors](ivec3 neighborWorldCell) {
            neighbors.push_back(neighborWorldCell);
        }
    );
    return neighbors;
}

// Line-of-sight check between two world cells (Bresenham-style 2D on x,y).
inline bool navLineOfSight(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    ivec3 from,
    ivec3 to,
    float agentClearance = 0.5f
) {
    int dx = to.x - from.x;
    int dy = to.y - from.y;
    int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    int sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int err = dx - dy;
    ivec3 cur = from;
    while (true) {
        if (!navIsPassable(navWorld, registry, cur, agentClearance)) return false;
        if (cur.x == to.x && cur.y == to.y) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cur.x += sx; }
        if (e2 < dx) { err += dx; cur.y += sy; }
    }
    return true;
}

// Remove redundant waypoints from an A* path using line-of-sight checks.
inline void smoothPath(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    std::vector<ivec3> &path,
    float agentClearance = 0.5f
) {
    if (path.size() <= 2) return;
    std::vector<ivec3> smoothed;
    smoothed.push_back(path.front());
    size_t anchor = 0;
    while (anchor < path.size() - 1) {
        size_t farthest = anchor + 1;
        for (size_t probe = anchor + 2; probe < path.size(); probe++) {
            if (navLineOfSight(navWorld, registry, path[anchor], path[probe], agentClearance)) {
                farthest = probe;
            }
        }
        smoothed.push_back(path[farthest]);
        anchor = farthest;
    }
    path = std::move(smoothed);
}

inline std::vector<ivec3> findPathAStarChunked(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    ivec3 startWorldCell,
    ivec3 endWorldCell,
    float agentClearance = 0.5f,
    int maxExpansions = 4000
) {
    std::vector<ivec3> path;
    if (!navCellExists(navWorld, registry, startWorldCell) || !navCellExists(navWorld, registry, endWorldCell)) {
        return path;
    }

    // If goal cell is walkable but lacks clearance for this agent, find the
    // nearest cell with sufficient clearance as a substitute goal.
    ivec3 effectiveGoal = endWorldCell;
    if (!navIsPassable(navWorld, registry, endWorldCell, agentClearance)) {
        float bestDist = 1e9f;
        bool found = false;
        int searchRad = static_cast<int>(std::ceil(agentClearance / navWorld.cellSizeWorld_)) + 2;
        for (int dx = -searchRad; dx <= searchRad; dx++) {
            for (int dy = -searchRad; dy <= searchRad; dy++) {
                ivec3 candidate = endWorldCell + ivec3(dx, dy, 0);
                if (navIsPassable(navWorld, registry, candidate, agentClearance)) {
                    float d = static_cast<float>(dx * dx + dy * dy);
                    if (d < bestDist) {
                        bestDist = d;
                        effectiveGoal = candidate;
                        found = true;
                    }
                }
            }
        }
        if (!found) return path;
    }

    struct Node {
        ivec3 cell;
        float gCost;
        float fCost;
        bool operator>(const Node &o) const { return fCost > o.fCost; }
    };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openSet;
    std::unordered_map<int64_t, float> gScore;
    std::unordered_map<int64_t, ivec3> cameFrom;
    gScore.reserve(static_cast<size_t>(maxExpansions));
    cameFrom.reserve(static_cast<size_t>(maxExpansions));

    // Allow pathfinding from start even if it has low clearance (unit may
    // already be near a wall). The A* neighbor filter will keep the path
    // within clearance-valid cells as soon as possible.
    gScore[worldCellToKey(startWorldCell)] = 0.0f;
    openSet.push({startWorldCell, 0.0f, heuristicChunked(startWorldCell, effectiveGoal)});

    int expansions = 0;
    ivec3 bestCell = startWorldCell;
    float bestH = heuristicChunked(startWorldCell, effectiveGoal);

    while (!openSet.empty()) {
        Node current = openSet.top();
        openSet.pop();

        if (current.cell == effectiveGoal) {
            ivec3 c = effectiveGoal;
            while (c.x != startWorldCell.x || c.y != startWorldCell.y || c.z != startWorldCell.z) {
                path.push_back(c);
                auto it = cameFrom.find(worldCellToKey(c));
                if (it == cameFrom.end()) break;
                c = it->second;
            }
            std::reverse(path.begin(), path.end());
            smoothPath(navWorld, registry, path, agentClearance);
            return path;
        }

        if (++expansions > maxExpansions) {
            ivec3 c = bestCell;
            while (c.x != startWorldCell.x || c.y != startWorldCell.y || c.z != startWorldCell.z) {
                path.push_back(c);
                auto it = cameFrom.find(worldCellToKey(c));
                if (it == cameFrom.end()) break;
                c = it->second;
            }
            std::reverse(path.begin(), path.end());
            smoothPath(navWorld, registry, path, agentClearance);
            return path;
        }

        float currentG = gScore[worldCellToKey(current.cell)];
        if (current.gCost > currentG) continue;

        float h = heuristicChunked(current.cell, effectiveGoal);
        if (h < bestH) {
            bestH = h;
            bestCell = current.cell;
        }

        navForEachPassableNeighbor(
            navWorld,
            registry,
            current.cell,
            agentClearance,
            [&](const ivec3 &neighbor) {
            float moveCost = moveCostForDelta(neighbor - current.cell);
            float tentativeG = currentG + moveCost;
            int64_t nk = worldCellToKey(neighbor);
            auto it = gScore.find(nk);
            if (it == gScore.end() || tentativeG < it->second) {
                cameFrom[nk] = current.cell;
                gScore[nk] = tentativeG;
                openSet.push({neighbor, tentativeG, tentativeG + heuristicChunked(neighbor, effectiveGoal)});
            }
            }
        );
    }
    return path;
}

// Reference-based overload: no getComponent calls.
inline bool navCircleOverlapsWall(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    float circleX,
    float circleY,
    float unitZ,
    float radius
) {
    float cellSizeWorld = navWorld.cellSizeWorld_;
    float halfCell = cellSizeWorld * 0.5f;
    float originX = navWorld.origin_.x;
    float originY = navWorld.origin_.y;
    float originZ = navWorld.origin_.z;

    int minCZ = static_cast<int>(std::ceil((originZ - unitZ - cellSizeWorld) / cellSizeWorld));
    int maxCZ = static_cast<int>(std::floor((originZ - unitZ) / cellSizeWorld));

    int minCX = static_cast<int>(std::floor((circleX - radius - halfCell - originX) / cellSizeWorld));
    int maxCX = static_cast<int>(std::ceil((circleX + radius + halfCell - originX) / cellSizeWorld));
    int minCY = static_cast<int>(std::floor((circleY - radius - halfCell - originY) / cellSizeWorld));
    int maxCY = static_cast<int>(std::ceil((circleY + radius + halfCell - originY) / cellSizeWorld));

    for (int cz = minCZ; cz <= maxCZ; cz++) {
        for (int cx = minCX; cx <= maxCX; cx++) {
            for (int cy = minCY; cy <= maxCY; cy++) {
                ivec3 worldCell(cx, cy, cz);
                ChunkCoord chunkCoord;
                ivec3 localCell;
                const auto *chunkData = getChunkDataForWorldCell(
                    navWorld, registry, worldCell, chunkCoord, localCell
                );
                if (!chunkData) continue;
                int localIdx = chunkData->getLocalIndex(localCell);
                if (localIdx < 0) continue;
                if (chunkData->walkable_[static_cast<size_t>(localIdx)]) continue;

                vec3 cellWorld = chunkData->localCellToWorld(localCell);
                float boxMinX = cellWorld.x - halfCell;
                float boxMaxX = cellWorld.x + halfCell;
                float boxMinY = cellWorld.y - halfCell;
                float boxMaxY = cellWorld.y + halfCell;
                float closestX = IRMath::clamp(circleX, boxMinX, boxMaxX);
                float closestY = IRMath::clamp(circleY, boxMinY, boxMaxY);
                float dx = circleX - closestX;
                float dy = circleY - closestY;
                float distSq = dx * dx + dy * dy;
                if (distSq < radius * radius) return true;
            }
        }
    }
    return false;
}

inline bool navCirclePathOverlapsWall(
    const IRComponents::C_NavWorld &navWorld,
    const IRComponents::C_ChunkRegistry &registry,
    float fromX,
    float fromY,
    float toX,
    float toY,
    float unitZ,
    float radius
) {
    float dx = toX - fromX;
    float dy = toY - fromY;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.001f) {
        return navCircleOverlapsWall(navWorld, registry, toX, toY, unitZ, radius);
    }

    float stepSize = std::max(
        0.5f,
        std::min(navWorld.cellSizeWorld_ * 0.5f, radius * 0.5f)
    );
    int steps = std::max(1, static_cast<int>(std::ceil(dist / stepSize)));

    for (int i = 1; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        float sampleX = fromX + dx * t;
        float sampleY = fromY + dy * t;
        if (navCircleOverlapsWall(navWorld, registry, sampleX, sampleY, unitZ, radius)) {
            return true;
        }
    }
    return false;
}

// ---- Legacy entity-ID overloads (delegate to reference versions) ----

inline ivec3 navWorldToCell(IREntity::EntityId levelEntityId, vec3 world) {
    auto &navWorld = IREntity::getComponent<IRComponents::C_NavWorld>(levelEntityId);
    return navWorldToCell(navWorld, world);
}

inline vec3 navCellToWorld(IREntity::EntityId levelEntityId, ivec3 worldCell) {
    auto &navWorld = IREntity::getComponent<IRComponents::C_NavWorld>(levelEntityId);
    auto &registry = IREntity::getComponent<IRComponents::C_ChunkRegistry>(levelEntityId);
    return navCellToWorld(navWorld, registry, worldCell);
}

inline bool navCellExists(IREntity::EntityId levelEntityId, ivec3 worldCell) {
    auto &navWorld = IREntity::getComponent<IRComponents::C_NavWorld>(levelEntityId);
    auto &registry = IREntity::getComponent<IRComponents::C_ChunkRegistry>(levelEntityId);
    return navCellExists(navWorld, registry, worldCell);
}

inline bool navIsPassable(IREntity::EntityId levelEntityId, ivec3 worldCell, float agentClearance = 0.5f) {
    auto &navWorld = IREntity::getComponent<IRComponents::C_NavWorld>(levelEntityId);
    auto &registry = IREntity::getComponent<IRComponents::C_ChunkRegistry>(levelEntityId);
    return navIsPassable(navWorld, registry, worldCell, agentClearance);
}

inline std::vector<ivec3> navGetNeighborCells(IREntity::EntityId levelEntityId, ivec3 worldCell) {
    auto &navWorld = IREntity::getComponent<IRComponents::C_NavWorld>(levelEntityId);
    auto &registry = IREntity::getComponent<IRComponents::C_ChunkRegistry>(levelEntityId);
    return navGetNeighborCells(navWorld, registry, worldCell);
}

inline std::vector<ivec3> findPathAStarChunked(
    IREntity::EntityId levelEntityId, ivec3 startWorldCell, ivec3 endWorldCell,
    float agentClearance = 0.5f, int maxExpansions = 4000
) {
    auto &navWorld = IREntity::getComponent<IRComponents::C_NavWorld>(levelEntityId);
    auto &registry = IREntity::getComponent<IRComponents::C_ChunkRegistry>(levelEntityId);
    return findPathAStarChunked(navWorld, registry, startWorldCell, endWorldCell, agentClearance, maxExpansions);
}

inline bool navCircleOverlapsWall(
    IREntity::EntityId levelEntityId, float circleX, float circleY,
    float unitZ, float radius, float /*cellSizeWorld*/
) {
    auto &navWorld = IREntity::getComponent<IRComponents::C_NavWorld>(levelEntityId);
    auto &registry = IREntity::getComponent<IRComponents::C_ChunkRegistry>(levelEntityId);
    return navCircleOverlapsWall(navWorld, registry, circleX, circleY, unitZ, radius);
}

inline bool navCirclePathOverlapsWall(
    IREntity::EntityId levelEntityId,
    float fromX,
    float fromY,
    float toX,
    float toY,
    float unitZ,
    float radius
) {
    auto &navWorld = IREntity::getComponent<IRComponents::C_NavWorld>(levelEntityId);
    auto &registry = IREntity::getComponent<IRComponents::C_ChunkRegistry>(levelEntityId);
    return navCirclePathOverlapsWall(navWorld, registry, fromX, fromY, toX, toY, unitZ, radius);
}

} // namespace IRMath

#endif /* NAV_QUERY_H */
