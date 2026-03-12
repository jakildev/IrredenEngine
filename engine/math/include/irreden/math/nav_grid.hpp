#ifndef IR_NAV_GRID_H
#define IR_NAV_GRID_H

#include <irreden/math/ir_math_types.hpp>

#include <optional>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace IRMath {

struct NavCell {
    ivec3 pos_;
    bool passable_;
    vec3 worldPosition_;

    NavCell()
        : pos_{0}
        , passable_{true}
        , worldPosition_{0.0f} {}

    NavCell(ivec3 pos, bool passable, vec3 worldPosition)
        : pos_{pos}
        , passable_{passable}
        , worldPosition_{worldPosition} {}
};

struct NavConnection {
    int fromCellIndex_;
    int toCellIndex_;
    float cost_;

    NavConnection()
        : fromCellIndex_{0}
        , toCellIndex_{0}
        , cost_{1.0f} {}

    NavConnection(int from, int to, float cost = 1.0f)
        : fromCellIndex_{from}
        , toCellIndex_{to}
        , cost_{cost} {}
};

class NavGrid {
  public:
    NavGrid() = default;
    NavGrid(float cellSizeWorld, float maxTraversableSlope = 1.0f)
        : m_cellSizeWorld{cellSizeWorld}
        , m_maxTraversableSlope{maxTraversableSlope} {}

    void setCellSize(float cellSizeWorld) { m_cellSizeWorld = cellSizeWorld; }
    float getCellSize() const { return m_cellSizeWorld; }
    void setOrigin(vec3 origin) { m_origin = origin; }
    vec3 getOrigin() const { return m_origin; }

    int addCell(ivec3 pos, bool passable = true);
    void addConnection(int fromCellIndex, int toCellIndex, float cost = 1.0f);
    void addConnectionByPos(ivec3 fromPos, ivec3 toPos, float cost = 1.0f);

    int getCellIndex(ivec3 pos) const;
    std::optional<int> getCellIndexOptional(ivec3 pos) const;
    const NavCell *getCell(int index) const;
    const NavCell *getCell(ivec3 pos) const;

    std::vector<int> getNeighbors(int cellIndex) const;
    float getConnectionCost(int fromCellIndex, int toCellIndex) const;

    vec3 cellToWorld(ivec3 pos) const;
    vec3 cellIndexToWorld(int cellIndex) const;
    ivec3 worldToCell(vec3 world) const;

    size_t getCellCount() const { return m_cells.size(); }
    const std::vector<NavCell> &getCells() const { return m_cells; }

    void clear();

  private:
    std::vector<NavCell> m_cells;
    std::vector<NavConnection> m_connections;
    std::unordered_map<int64_t, int> m_posToIndex;
    float m_cellSizeWorld = 1.0f;
    float m_maxTraversableSlope = 1.0f;
    vec3 m_origin = vec3(0.0f);

    static int64_t posToKey(ivec3 pos);
};

} // namespace IRMath

#endif /* IR_NAV_GRID_H */
