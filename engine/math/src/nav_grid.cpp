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
    auto it = m_posToIndex.find(key);
    if (it != m_posToIndex.end()) {
        return it->second;
    }
    vec3 worldPos = cellToWorld(pos);
    m_cells.emplace_back(pos, passable, worldPos);
    int index = static_cast<int>(m_cells.size() - 1);
    m_posToIndex[key] = index;
    return index;
}

void NavGrid::addConnection(int fromCellIndex, int toCellIndex, float cost) {
    if (fromCellIndex >= 0 && fromCellIndex < static_cast<int>(m_cells.size()) &&
        toCellIndex >= 0 && toCellIndex < static_cast<int>(m_cells.size())) {
        m_connections.emplace_back(fromCellIndex, toCellIndex, cost);
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
    auto it = m_posToIndex.find(posToKey(pos));
    return (it != m_posToIndex.end()) ? it->second : -1;
}

std::optional<int> NavGrid::getCellIndexOptional(ivec3 pos) const {
    int idx = getCellIndex(pos);
    return (idx >= 0) ? std::optional<int>(idx) : std::nullopt;
}

const NavCell *NavGrid::getCell(int index) const {
    if (index >= 0 && index < static_cast<int>(m_cells.size())) {
        return &m_cells[static_cast<size_t>(index)];
    }
    return nullptr;
}

const NavCell *NavGrid::getCell(ivec3 pos) const {
    int idx = getCellIndex(pos);
    return getCell(idx);
}

std::vector<int> NavGrid::getNeighbors(int cellIndex) const {
    std::vector<int> neighbors;
    for (const auto &conn : m_connections) {
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
    for (const auto &conn : m_connections) {
        if (conn.fromCellIndex_ == fromCellIndex && conn.toCellIndex_ == toCellIndex) {
            return conn.cost_;
        }
    }
    return 1.0f;
}

vec3 NavGrid::cellToWorld(ivec3 pos) const {
    return vec3(
        static_cast<float>(pos.x) * m_cellSizeWorld,
        static_cast<float>(pos.y) * m_cellSizeWorld,
        static_cast<float>(pos.z) * m_cellSizeWorld
    ) + m_origin;
}

vec3 NavGrid::cellIndexToWorld(int cellIndex) const {
    const NavCell *cell = getCell(cellIndex);
    return cell ? cell->worldPosition_ : vec3(0.0f);
}

ivec3 NavGrid::worldToCell(vec3 world) const {
    vec3 scaled = (world - m_origin) / m_cellSizeWorld;
    return ivec3(IRMath::floor(scaled.x), IRMath::floor(scaled.y), IRMath::floor(scaled.z));
}

void NavGrid::clear() {
    m_cells.clear();
    m_connections.clear();
    m_posToIndex.clear();
}

} // namespace IRMath
