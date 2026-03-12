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
        : cellSizeWorld_{cellSizeWorld}
        , maxTraversableSlope_{maxTraversableSlope} {}

    void setCellSize(float cellSizeWorld) { cellSizeWorld_ = cellSizeWorld; }
    float getCellSize() const { return cellSizeWorld_; }
    void setOrigin(vec3 origin) { origin_ = origin; }
    vec3 getOrigin() const { return origin_; }

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

    size_t getCellCount() const { return cells_.size(); }
    const std::vector<NavCell> &getCells() const { return cells_; }

    void clear();

  private:
    std::vector<NavCell> cells_;
    std::vector<NavConnection> connections_;
    std::unordered_map<int64_t, int> posToIndex_;
    float cellSizeWorld_ = 1.0f;
    float maxTraversableSlope_ = 1.0f;
    vec3 origin_ = vec3(0.0f);

    static int64_t posToKey(ivec3 pos);
};

} // namespace IRMath

#endif /* IR_NAV_GRID_H */
