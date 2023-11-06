#ifndef COMPONENT_GAME_GRID_H
#define COMPONENT_GAME_GRID_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_ecs.hpp>

#include <vector>

using IRMath::uvec2;
using IRECS::EntityId;

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

        C_GameGrid2D()
        {

        }

        EntityId& at(const uvec2 position) {
            return grid_.at(position.y * size_.x + position.x);
        }

    };

} // namespace IRComponents


#endif /* COMPONENT_GAME_GRID_H */
