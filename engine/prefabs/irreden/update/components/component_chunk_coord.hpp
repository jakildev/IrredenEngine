#ifndef COMPONENT_CHUNK_COORD_H
#define COMPONENT_CHUNK_COORD_H

#include <irreden/math/nav_types.hpp>

namespace IRComponents {

struct C_ChunkCoord {
    IRMath::ChunkCoord coord_;

    C_ChunkCoord()
        : coord_{0, 0} {}

    C_ChunkCoord(IRMath::ChunkCoord coord)
        : coord_{coord} {}
};

} // namespace IRComponents

#endif /* COMPONENT_CHUNK_COORD_H */
