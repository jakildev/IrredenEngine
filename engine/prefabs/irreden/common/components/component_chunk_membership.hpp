#ifndef COMPONENT_CHUNK_MEMBERSHIP_H
#define COMPONENT_CHUNK_MEMBERSHIP_H

// Which chunk does this entity belong to? Pure-data tag, written by the
// chunk-membership migration system (Topic 5 of
// docs/design/world-streaming.md). NOT auto-attached by createEntity —
// entities in single-chunk creations carry no chunk metadata, so the
// non-streaming path stays zero-overhead.

#include <irreden/ir_math.hpp>

namespace IRComponents {

struct C_ChunkMembership {
    IRMath::ivec3 chunkCoord_{0, 0, 0};

    C_ChunkMembership() = default;

    explicit C_ChunkMembership(IRMath::ivec3 chunkCoord)
        : chunkCoord_{chunkCoord} {}
};

} // namespace IRComponents

#endif /* COMPONENT_CHUNK_MEMBERSHIP_H */
