#ifndef IR_TEST_FOG_WINDOW_IMAGE_H
#define IR_TEST_FOG_WINDOW_IMAGE_H

// PURPOSE: Expand a whole fog window into a CPU image through the gather's
//   own planner and expansion, for tests that assert texel contents.

#include <irreden/ir_math.hpp>
#include <irreden/render/fog_world_field.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace IRTest {

/// Expands the whole window of @p edge at @p origin into @p image (edge²
/// texels), every texel first filled with @p unwritten so an expansion that
/// skips one is unmistakable.
inline void expandWholeFogWindow(
    IRPrefab::Fog::WorldField &field,
    IRMath::ivec2 origin,
    int edge,
    std::vector<IRPrefab::Fog::FogWindowTexel> &image,
    IRPrefab::Fog::FogWindowTexel unwritten = {}
) {
    using IRPrefab::Fog::detail::WindowUploadRect;
    IRPrefab::Fog::detail::WindowGatherPlan plan;
    IRPrefab::Fog::detail::planWindowGather(std::nullopt, origin, edge, {}, plan);
    image.assign(static_cast<std::size_t>(edge) * static_cast<std::size_t>(edge), unwritten);
    std::vector<IRPrefab::Fog::FogWindowTexel> strip;
    for (const WindowUploadRect &rect : plan.rects_) {
        strip.assign(static_cast<std::size_t>(rect.size_.x * rect.size_.y), unwritten);
        IRPrefab::Fog::detail::expandWindowChunks(field, origin, edge, rect, strip);
        for (int y = 0; y < rect.size_.y; ++y) {
            std::copy_n(
                strip.begin() + static_cast<std::ptrdiff_t>(y * rect.size_.x),
                rect.size_.x,
                image.begin() +
                    static_cast<std::ptrdiff_t>((rect.texel_.y + y) * edge + rect.texel_.x)
            );
        }
    }
}

/// The texel of world @p column in a whole-window @p image of @p edge.
inline IRPrefab::Fog::FogWindowTexel fogWindowTexelOf(
    const std::vector<IRPrefab::Fog::FogWindowTexel> &image, IRMath::ivec2 column, int edge
) {
    const IRMath::ivec2 texel = IRPrefab::Fog::detail::windowTexel(column, edge);
    return image[static_cast<std::size_t>(texel.y * edge + texel.x)];
}

} // namespace IRTest

#endif /* IR_TEST_FOG_WINDOW_IMAGE_H */
