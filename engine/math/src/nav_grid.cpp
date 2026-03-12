#include <irreden/math/nav_grid.hpp>
#include <irreden/ir_math.hpp>

namespace IRMath {

int64_t NavGrid::posToKey(ivec3 pos) {
    return (static_cast<int64_t>(pos.x) << 42) |
           (static_cast<int64_t>(pos.y) << 21) |
           (static_cast<int64_t>(pos.z) & 0x1FFFFF);
}

int NavGrid::addCell(ivec3 pos, bool passable) {
    int64_t key = posToKey(pos);
    auto it = posToIndex_.find(key);
    if (it != posToIndex_.end()) {
        return it->second;
    }
    vec3 worldPos = cellToWorld(pos);
    cells_.emplace_back(pos, passable, worldPos);
    int index = static_cast<int>(cells_.size() - 1);
    posToIndex_[key] = index;
    return index;
}

void NavGrid::addConnection(int fromCellIndex, int toCellIndex, float cost) {
    if (fromCellIndex >= 0 && fromCellIndex < static_cast<int>(cells_.size()) &&
        toCellIndex >= 0 && toCellIndex < static_cast<int>(cells_.size())) {
        connections_.emplace_back(fromCellIndex, toCellIndex, cost);
    }
}

void NavGrid::addConnectionByPos(ivec3 fromPos, ivec3 toPos, float cost) {
    int fromIdx = getCellIndex(fromPos);
    int toIdx = getCellIndex(toPos);
    if (fromIdx >= 0 && toIdx >= 0) {
        addConnection(fromIdx, toIdx, cost);
    }
}

int NavGrid::getCellIndex(ivec3 pos) const {
    auto it = posToIndex_.find(posToKey(pos));
    return (it != posToIndex_.end()) ? it->second : -1;
}

std::optional<int> NavGrid::getCellIndexOptional(ivec3 pos) const {
    int idx = getCellIndex(pos);
    return (idx >= 0) ? std::optional<int>(idx) : std::nullopt;
}

const NavCell *NavGrid::getCell(int index) const {
    if (index >= 0 && index < static_cast<int>(cells_.size())) {
        return &cells_[static_cast<size_t>(index)];
    }
    return nullptr;
}

const NavCell *NavGrid::getCell(ivec3 pos) const {
    int idx = getCellIndex(pos);
    return getCell(idx);
}

std::vector<int> NavGrid::getNeighbors(int cellIndex) const {
    std::vector<int> neighbors;
    for (const auto &conn : connections_) {
        if (conn.fromCellIndex_ == cellIndex) {
            const NavCell *toCell = getCell(conn.toCellIndex_);
            if (toCell && toCell->passable_) {
                neighbors.push_back(conn.toCellIndex_);
            }
        }
    }
    return neighbors;
}

float NavGrid::getConnectionCost(int fromCellIndex, int toCellIndex) const {
    for (const auto &conn : connections_) {
        if (conn.fromCellIndex_ == fromCellIndex && conn.toCellIndex_ == toCellIndex) {
            return conn.cost_;
        }
    }
    return 1.0f;
}

vec3 NavGrid::cellToWorld(ivec3 pos) const {
    return vec3(
        static_cast<float>(pos.x) * cellSizeWorld_,
        static_cast<float>(pos.y) * cellSizeWorld_,
        static_cast<float>(pos.z) * cellSizeWorld_
    ) + origin_;
}

vec3 NavGrid::cellIndexToWorld(int cellIndex) const {
    const NavCell *cell = getCell(cellIndex);
    return cell ? cell->worldPosition_ : vec3(0.0f);
}

ivec3 NavGrid::worldToCell(vec3 world) const {
    vec3 scaled = (world - origin_) / cellSizeWorld_;
    return ivec3(IRMath::floor(scaled.x), IRMath::floor(scaled.y), IRMath::floor(scaled.z));
}

void NavGrid::clear() {
    cells_.clear();
    connections_.clear();
    posToIndex_.clear();
}

} // namespace IRMath
