#ifndef LOD_UTILS_H
#define LOD_UTILS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

using namespace IRMath;

namespace IRRender {

inline std::uint32_t computeLodLevel(float zoomLevel) {
    if (zoomLevel >= 4.0f) return 0;
    if (zoomLevel >= 2.0f) return 1;
    if (zoomLevel >= 1.0f) return 2;
    if (zoomLevel >= 0.5f) return 3;
    return 4;
}

inline float lodVoxelScale(std::uint32_t lodLevel) {
    switch (lodLevel) {
        case 0: return 1.0f;
        case 1: return 0.75f;
        case 2: return 0.5f;
        case 3: return 0.25f;
        default: return 0.125f;
    }
}

inline bool shouldSkipAtLod(std::uint32_t entityLodMin, std::uint32_t currentLod) {
    return currentLod > entityLodMin + 2;
}

} // namespace IRRender

#endif /* LOD_UTILS_H */
