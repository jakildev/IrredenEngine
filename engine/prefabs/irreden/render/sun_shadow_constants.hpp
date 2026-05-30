#ifndef IRREDEN_RENDER_SUN_SHADOW_CONSTANTS_H
#define IRREDEN_RENDER_SUN_SHADOW_CONSTANTS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/cull_viewport_state.hpp>

namespace IRPrefab::SunShadow {

// Maximum shadow throw distance in voxels. Drives both the sun-depth-map
// AABB sweep in BAKE_SUN_SHADOW_MAP and the cull-region widening in the
// iso rasterizers (VOXEL_TO_TRIXEL_*, SHAPES_TO_TRIXEL); they must agree
// or off-screen casters within shadow range either fail to write the
// shadow source (rasterizer too narrow) or fail to project it onto a
// visible pixel (bake too narrow).
constexpr float kSunShadowMaxDistance = 64.0f;

// Resolves the active sun direction. Honors a `C_LightSource{type=DIRECTIONAL}`
// entity's `direction_` if present (matches BAKE_SUN_SHADOW_MAP's resolution
// in detail::resolveSun), otherwise falls back to `IRRender::getSunDirection()`.
// Returned vector is unit-length when the override is non-zero; the global
// path is unit-length by `setSunDirection` invariant.
inline IRMath::vec3 resolveDirection() {
    IRMath::vec3 dir = IRRender::getSunDirection();
    const auto include = IREntity::getArchetype<IRComponents::C_LightSource>();
    auto nodes = IREntity::queryArchetypeNodesSimple(include);
    for (auto *node : nodes) {
        auto &lights = IREntity::getComponentData<IRComponents::C_LightSource>(node);
        for (int i = 0; i < node->length_; ++i) {
            const IRComponents::C_LightSource &light = lights[i];
            if (light.type_ != IRComponents::LightType::DIRECTIONAL) {
                continue;
            }
            const float length = IRMath::length(light.direction_);
            if (length > 0.0f) {
                dir = light.direction_ / length;
            }
        }
    }
    return dir;
}

// Per-frame resolved sun direction. Call from a system's beginTick (once per
// frame), store the result in SystemParams, and read the cached value in the
// per-entity tick. Identical to resolveDirection() today; reserved for future
// caching by a RESOLVE_SUN_DIRECTION head-of-pipeline system.
inline IRMath::vec3 getFrameSunDirection() {
    return resolveDirection();
}

// Per-frame shadow-feeder inputs: the active sun direction and the iso-AABB
// sweep length, each gated on whether sun shadows are enabled (disabled →
// zero, the no-widen case for IRMath::shadowFeederIsoBounds). The
// C_LightSource archetype scan behind getFrameSunDirection() runs only when
// shadows are on, so resolve this ONCE per frame (a system's beginTick) and
// reuse the result; don't re-resolve per iterating entity/canvas.
struct ShadowFeederParams {
    IRMath::vec3 sunDir_ = IRMath::vec3(0.0f);
    float sweepDistance_ = 0.0f;
};

inline ShadowFeederParams frameShadowFeederParams() {
    if (!IRRender::getSunShadowsEnabled()) {
        return {};
    }
    return {getFrameSunDirection(), kSunShadowMaxDistance};
}

// The shared frozen-or-live cull viewport at @p margin, widened to the
// shadow-feeder AABB so off-screen casters within shadow range still feed the
// sun-depth bake (see IRMath::shadowFeederIsoBounds). Pass the once-per-frame
// @p params from frameShadowFeederParams(); a caller needing several margins
// (the voxel rasterizer's chunk + GPU bounds) reuses a single params resolve.
// Centralizes the cull-viewport → shadow-feeder derivation shared by
// VOXEL_TO_TRIXEL_STAGE_1, SHAPES_TO_TRIXEL, and REBUILD_GRID_VOXELS.
inline IRMath::IsoBounds2D shadowFeederCullViewport(int margin, const ShadowFeederParams &params) {
    return IRMath::shadowFeederIsoBounds(
        IRRender::getCullViewport().isoViewport(margin),
        params.sunDir_,
        params.sweepDistance_
    );
}

} // namespace IRPrefab::SunShadow

#endif /* IRREDEN_RENDER_SUN_SHADOW_CONSTANTS_H */
