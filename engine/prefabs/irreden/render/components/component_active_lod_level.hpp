#ifndef COMPONENT_ACTIVE_LOD_LEVEL_H
#define COMPONENT_ACTIVE_LOD_LEVEL_H

#include <irreden/ir_render.hpp>

namespace IRComponents {

// Singleton row carrying the LOD tier the renderer should use this frame.
// Written by the LOD_UPDATE system (UPDATE pipeline, reads camera zoom); tier
// consumers read it once per tick through IRPrefab::Lod::TierSnapshot.
//
// Default is LOD_4 (coarsest tier) so a creation that doesn't register
// LOD_UPDATE — and therefore never writes the singleton — still draws every
// default-band entity. A missing row resolves the same way.
struct C_ActiveLodLevel {
    IRRender::LodLevel current_ = IRRender::LodLevel::LOD_4;
};

} // namespace IRComponents

#endif /* COMPONENT_ACTIVE_LOD_LEVEL_H */
