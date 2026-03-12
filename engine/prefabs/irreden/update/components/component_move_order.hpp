#ifndef COMPONENT_MOVE_ORDER_H
#define COMPONENT_MOVE_ORDER_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

struct C_MoveOrder {
    IRMath::ivec3 targetCell_;

    C_MoveOrder()
        : targetCell_{0, 0, 0} {}

    C_MoveOrder(IRMath::ivec3 targetCell)
        : targetCell_{targetCell} {}
};

} // namespace IRComponents

#endif /* COMPONENT_MOVE_ORDER_H */
