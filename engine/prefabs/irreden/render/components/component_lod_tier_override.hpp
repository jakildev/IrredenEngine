#ifndef COMPONENT_LOD_TIER_OVERRIDE_H
#define COMPONENT_LOD_TIER_OVERRIDE_H

#include <irreden/ir_profile.hpp>
#include <irreden/render/lod_level.hpp>

namespace IRComponents {

// Pins the LOD tier an entity resolves to in the world, regardless of camera
// zoom. Every world tier consumer resolves through IRRender::resolveEntityLod
// (lod_utils.hpp), so an entity carrying this draws the band member matching
// `tier_` at every zoom. Removing the component returns the entity to the
// zoom-derived tier. A secondary viewport bands its subjects at its own zoom's
// tier and ignores this pin.
struct C_LodTierOverride {
    IRRender::LodLevel tier_ = IRRender::LodLevel::LOD_0;

    C_LodTierOverride() = default;

    // The Lua usertype ctor binds this overload directly and sol2 converts any
    // integer to the enum, so the range check lives here for both callers.
    explicit C_LodTierOverride(IRRender::LodLevel tier)
        : tier_{tier} {
        IR_ASSERT(tier <= IRRender::LodLevel::LOD_4, "C_LodTierOverride: LodLevel out of range");
    }
};

} // namespace IRComponents

#endif /* COMPONENT_LOD_TIER_OVERRIDE_H */
