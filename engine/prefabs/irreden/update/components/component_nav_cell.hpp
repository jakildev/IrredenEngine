#ifndef COMPONENT_NAV_CELL_H
#define COMPONENT_NAV_CELL_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

/// Nav cell for baking. Clearance = largest agent radius (world units) that can traverse.
/// Industry pattern: clearance fields avoid per-path collision checks.
struct C_NavCell {
    IRMath::ivec3 gridPos_;
    bool passable_;
    float clearance_;  ///< World units; <= 0 = use baked default

    C_NavCell()
        : gridPos_{0, 0, 0}
        , passable_{true}
        , clearance_{0.0f} {}

    C_NavCell(IRMath::ivec3 gridPos, bool passable = true, float clearance = 0.0f)
        : gridPos_{gridPos}
        , passable_{passable}
        , clearance_{clearance} {}
};

} // namespace IRComponents

#endif /* COMPONENT_NAV_CELL_H */
