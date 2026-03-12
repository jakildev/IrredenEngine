#ifndef IR_NAV_TYPES_H
#define IR_NAV_TYPES_H

#include <irreden/ir_math.hpp>

namespace IRMath {

using ChunkCoord = ivec2;

inline ChunkCoord worldCellToChunkCoord(ivec3 worldCell, ivec3 chunkSize) {
    int cx = chunkSize.x > 0 ? (worldCell.x >= 0 ? worldCell.x / chunkSize.x : (worldCell.x - chunkSize.x + 1) / chunkSize.x) : 0;
    int cy = chunkSize.y > 0 ? (worldCell.y >= 0 ? worldCell.y / chunkSize.y : (worldCell.y - chunkSize.y + 1) / chunkSize.y) : 0;
    return ivec2(cx, cy);
}

inline ivec3 worldCellToLocalCell(ivec3 worldCell, ChunkCoord chunk, ivec3 chunkSize) {
    return ivec3(
        worldCell.x - chunk.x * chunkSize.x,
        worldCell.y - chunk.y * chunkSize.y,
        worldCell.z
    );
}

inline ivec3 localCellToWorldCell(ivec3 localCell, ChunkCoord chunk, ivec3 chunkSize) {
    return ivec3(
        localCell.x + chunk.x * chunkSize.x,
        localCell.y + chunk.y * chunkSize.y,
        localCell.z
    );
}

inline int64_t posToKey(ivec3 pos) {
    return (static_cast<int64_t>(pos.x) << 42) |
           (static_cast<int64_t>(pos.y) << 21) |
           (static_cast<int64_t>(pos.z) & 0x1FFFFF);
}

inline int64_t chunkCoordToKey(ChunkCoord c) {
    return (static_cast<int64_t>(c.x) << 32) | (static_cast<int64_t>(c.y) & 0xFFFFFFFF);
}

} // namespace IRMath

#endif /* IR_NAV_TYPES_H */
