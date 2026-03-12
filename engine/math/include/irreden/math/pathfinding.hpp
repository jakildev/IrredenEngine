#ifndef IR_PATHFINDING_H
#define IR_PATHFINDING_H

#include <irreden/math/nav_grid.hpp>

#include <vector>

namespace IRMath {

std::vector<ivec3> findPathAStar(
    const NavGrid &grid,
    ivec3 start,
    ivec3 end
);

float getConnectionCost(const NavGrid &grid, int fromCellIndex, int toCellIndex);

} // namespace IRMath

#endif /* IR_PATHFINDING_H */
