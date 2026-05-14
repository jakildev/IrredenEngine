#ifndef COMPONENT_ACTIVE_LOD_LEVEL_H
#define COMPONENT_ACTIVE_LOD_LEVEL_H

#include <irreden/ir_render.hpp>

namespace IRComponents {

// Singleton row carrying the LOD tier the renderer should use this frame.
// Written by the LOD_UPDATE system (UPDATE pipeline, reads camera zoom),
// read by SHAPES_TO_TRIXEL in beginTick to filter shapes by lodMin_.
//
// Default is LOD_4 (coarsest tier, no culling) so a creation that doesn't
// register LOD_UPDATE — and therefore never writes the singleton —
// still gets the every-shape-visible behavior. The shapes filter reads
// the singleton via singletonOrNull<>; missing row also resolves to
// "no culling".
struct C_ActiveLodLevel {
    IRRender::LodLevel current_ = IRRender::LodLevel::LOD_4;
};

} // namespace IRComponents

#endif /* COMPONENT_ACTIVE_LOD_LEVEL_H */
