#include <irreden/math/pathfinding.hpp>
#include <irreden/ir_math.hpp>

#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_map>

namespace IRMath {

namespace {

struct AStarNode {
    int cellIndex;
    float gCost;
    float fCost;

    bool operator>(const AStarNode &other) const {
        return fCost > other.fCost;
    }
};

float heuristic(ivec3 a, ivec3 b) {
    int dx = IRMath::abs(a.x - b.x);
    int dy = IRMath::abs(a.y - b.y);
    int dz = IRMath::abs(a.z - b.z);
    return static_cast<float>(dx + dy + dz);
}

} // namespace

float getConnectionCost(const NavGrid &grid, int fromCellIndex, int toCellIndex) {
    float explicitCost = grid.getConnectionCost(fromCellIndex, toCellIndex);
    if (explicitCost > 0) {
        return explicitCost;
    }
    const NavCell *fromCell = grid.getCell(fromCellIndex);
    const NavCell *toCell = grid.getCell(toCellIndex);
    if (!fromCell || !toCell) {
        return std::numeric_limits<float>::max();
    }
    float baseCost = 1.0f;
    int heightDiff = IRMath::abs(toCell->pos_.z - fromCell->pos_.z);
    if (heightDiff > 0) {
        baseCost += static_cast<float>(heightDiff) * 0.5f;
    }
    return baseCost;
}

std::vector<ivec3> findPathAStar(const NavGrid &grid, ivec3 start, ivec3 end) {
    std::vector<ivec3> path;
    int startIdx = grid.getCellIndex(start);
    int endIdx = grid.getCellIndex(end);
    if (startIdx < 0 || endIdx < 0) {
        return path;
    }

    const NavCell *endCell = grid.getCell(endIdx);
    if (!endCell || !endCell->passable_) {
        return path;
    }

    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;
    std::unordered_map<int, float> gScore;
    std::unordered_map<int, int> cameFrom;

    gScore[startIdx] = 0.0f;
    ivec3 endPos = grid.getCell(endIdx)->pos_;
    openSet.push({
        startIdx,
        0.0f,
        heuristic(grid.getCell(startIdx)->pos_, endPos)
    });

    while (!openSet.empty()) {
        AStarNode current = openSet.top();
        openSet.pop();

        if (current.cellIndex == endIdx) {
            int idx = endIdx;
            while (idx >= 0) {
                const NavCell *cell = grid.getCell(idx);
                if (cell) {
                    path.push_back(cell->pos_);
                }
                auto it = cameFrom.find(idx);
                idx = (it != cameFrom.end()) ? it->second : -1;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        float currentG = gScore[current.cellIndex];
        if (current.gCost > currentG) {
            continue;
        }

        std::vector<int> neighbors = grid.getNeighbors(current.cellIndex);
        for (int neighborIdx : neighbors) {
            const NavCell *neighborCell = grid.getCell(neighborIdx);
            if (!neighborCell || !neighborCell->passable_) {
                continue;
            }

            float moveCost = grid.getConnectionCost(current.cellIndex, neighborIdx);
            float tentativeG = currentG + moveCost;

            auto it = gScore.find(neighborIdx);
            if (it == gScore.end() || tentativeG < it->second) {
                cameFrom[neighborIdx] = current.cellIndex;
                gScore[neighborIdx] = tentativeG;
                float f = tentativeG + heuristic(neighborCell->pos_, endPos);
                openSet.push({neighborIdx, tentativeG, f});
            }
        }
    }

    return path;
}

} // namespace IRMath
