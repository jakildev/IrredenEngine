#ifndef IRREDEN_RENDER_LOD_LEVEL_H
#define IRREDEN_RENDER_LOD_LEVEL_H

#include <cstdint>

namespace IRRender {

// LOD tier index. Goes DOWN as detail goes UP: LOD_0 is highest-detail
// (close zoom), LOD_4 is the coarsest silhouette tier (always drawn).
// SHAPES_TO_TRIXEL filters C_ShapeDescriptor against the active tier
// (see engine/prefabs/irreden/render/lod_utils.hpp).
//
// Split out of ir_render_types.hpp so consumers (component_shape_descriptor,
// component_active_lod_level) that only need the enum can pick it up
// without pulling the umbrella render types header. Mirrors the active_canvas
// split (T-205) — see #739.
enum class LodLevel : std::uint32_t {
    LOD_0 = 0,
    LOD_1 = 1,
    LOD_2 = 2,
    LOD_3 = 3,
    LOD_4 = 4,
};

} // namespace IRRender

#endif /* IRREDEN_RENDER_LOD_LEVEL_H */
