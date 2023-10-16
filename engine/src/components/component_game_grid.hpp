#ifndef COMPONENT_GAME_GRID_H
#define COMPONENT_GAME_GRID_H

#include "../math/ir_math.hpp"
#include "../entity/ir_ecs.hpp"
#include <vector>

using namespace IRMath;

namespace IRComponents {

    struct C_GameGrid2D {
        uvec2 size_;
        std::vector<EntityId> grid_;

        C_GameGrid2D(uvec2 size)
        :   size_{size},
            grid_{}
        {
            grid_.resize(size.x * size.y);
            std::fill(grid_.begin(), grid_.end(), IRECS::kNullEntityId);
        }

        C_GameGrid2D(unsigned int x, unsigned int y)
        :   C_GameGrid2D{uvec2(x, y)}
        {

        }

        // Default
        C_GameGrid2D()
        {

        }

        EntityId& at(const uvec2 position) {
            return grid_.at(position.y * size_.x + position.x);
        }

    };

} // namespace IRComponents


#endif /* COMPONENT_GAME_GRID_H */
