#ifndef ISO_SPATIAL_HASH_H
#define ISO_SPATIAL_HASH_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace IRMath;

namespace IRRender {

class IsoSpatialHash {
  public:
    IsoSpatialHash(int cellSize = 32)
        : m_cellSize{cellSize} {}

    void clear() {
        m_cells.clear();
    }

    void insert(IREntity::EntityId entity, vec2 isoMin, vec2 isoMax) {
        int x0 = static_cast<int>(std::floor(isoMin.x / m_cellSize));
        int y0 = static_cast<int>(std::floor(isoMin.y / m_cellSize));
        int x1 = static_cast<int>(std::floor(isoMax.x / m_cellSize));
        int y1 = static_cast<int>(std::floor(isoMax.y / m_cellSize));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                m_cells[cellKey(x, y)].insert(entity);
            }
        }
    }

    std::vector<IREntity::EntityId> query(vec2 viewMin, vec2 viewMax) const {
        std::unordered_set<IREntity::EntityId> result;
        int x0 = static_cast<int>(std::floor(viewMin.x / m_cellSize));
        int y0 = static_cast<int>(std::floor(viewMin.y / m_cellSize));
        int x1 = static_cast<int>(std::floor(viewMax.x / m_cellSize));
        int y1 = static_cast<int>(std::floor(viewMax.y / m_cellSize));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                auto it = m_cells.find(cellKey(x, y));
                if (it != m_cells.end()) {
                    result.insert(it->second.begin(), it->second.end());
                }
            }
        }
        return std::vector<IREntity::EntityId>(result.begin(), result.end());
    }

  private:
    int m_cellSize;
    std::unordered_map<std::int64_t, std::unordered_set<IREntity::EntityId>> m_cells;

    static std::int64_t cellKey(int x, int y) {
        return (static_cast<std::int64_t>(x) << 32) | (static_cast<std::int64_t>(y) & 0xFFFFFFFF);
    }
};

} // namespace IRRender

#endif /* ISO_SPATIAL_HASH_H */
