#ifndef IRREDEN_RENDER_SUN_SHADOW_CONSTANTS_H
#define IRREDEN_RENDER_SUN_SHADOW_CONSTANTS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_light_source.hpp>

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

} // namespace IRPrefab::SunShadow

#endif /* IRREDEN_RENDER_SUN_SHADOW_CONSTANTS_H */
