#ifndef COMPONENT_NAV_GRID_H
#define COMPONENT_NAV_GRID_H

#include <irreden/math/nav_grid.hpp>

#include <memory>

namespace IRComponents {

struct C_NavGrid {
    std::shared_ptr<IRMath::NavGrid> grid_;

    C_NavGrid()
        : grid_{std::make_shared<IRMath::NavGrid>(1.0f)} {}

    C_NavGrid(float cellSizeWorld)
        : grid_{std::make_shared<IRMath::NavGrid>(cellSizeWorld)} {}

    IRMath::NavGrid *get() { return grid_.get(); }
    const IRMath::NavGrid *get() const { return grid_.get(); }
};

} // namespace IRComponents

#endif /* COMPONENT_NAV_GRID_H */
