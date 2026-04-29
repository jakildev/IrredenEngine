#ifndef COMPONENT_LIGHT_SOURCE_H
#define COMPONENT_LIGHT_SOURCE_H

#include <cstdint>
#include <irreden/ir_math.hpp>

namespace IRComponents {

/// Classification of a light-emitting entity. Used by lighting phases to
/// determine which algorithm processes this light source.
/// - DIRECTIONAL: parallel rays from infinite distance (e.g. sun/moon)
/// - POINT: omnidirectional point emitter; direction_ is ignored
/// - EMISSIVE: voxel-local glow, no long-range propagation; direction_ is ignored
/// - SPOT: cone emitter using direction_ and a per-entity cone angle
enum class LightType : int {
    DIRECTIONAL = 0,
    POINT = 1,
    EMISSIVE = 2,
    SPOT = 3,
};

/// Light source component: marks an entity as a light emitter for all
/// lighting phases (AO, shadows, flood-fill, fog-of-war LOS).
struct C_LightSource {
    LightType type_;
    IRMath::Color emitColor_;
    float intensity_;
    /// Maximum propagation radius in voxel blocks. uint8_t caps flood-fill BFS
    /// at 255 blocks, matching the 256³ world chunk size.
    uint8_t radius_;
    /// World-space unit vector for DIRECTIONAL and SPOT lights.
    /// Ignored for POINT and EMISSIVE.
    IRMath::vec3 direction_;
    /// Full cone aperture in degrees for SPOT lights.
    /// Ignored for DIRECTIONAL, POINT, and EMISSIVE.
    float coneAngleDeg_;
    /// Ambient floor for DIRECTIONAL face shading. Ignored by non-sun lights.
    float ambient_;

    C_LightSource(
        LightType type,
        IRMath::Color emitColor,
        float intensity,
        uint8_t radius,
        IRMath::vec3 direction,
        float coneAngleDeg,
        float ambient
    )
        : type_{type}
        , emitColor_{emitColor}
        , intensity_{intensity}
        , radius_{radius}
        , direction_{direction}
        , coneAngleDeg_{coneAngleDeg}
        , ambient_{ambient} {}

    C_LightSource(
        LightType type,
        IRMath::Color emitColor,
        float intensity,
        uint8_t radius,
        IRMath::vec3 direction,
        float coneAngleDeg
    )
        : C_LightSource{type, emitColor, intensity, radius, direction, coneAngleDeg, 0.4f} {}

    C_LightSource(
        LightType type,
        IRMath::Color emitColor,
        float intensity,
        uint8_t radius,
        IRMath::vec3 direction
    )
        : C_LightSource{type, emitColor, intensity, radius, direction, 45.0f} {}

    C_LightSource(LightType type, IRMath::Color emitColor, float intensity, uint8_t radius)
        : C_LightSource{type, emitColor, intensity, radius, IRMath::vec3{0.0f, -1.0f, 0.0f}} {}

    C_LightSource()
        : C_LightSource{
              LightType::EMISSIVE,
              IRMath::IRColors::kWhite,
              1.0f,
              4,
              IRMath::vec3{0.0f, 0.0f, 0.0f},
              45.0f,
              0.4f
          } {}
};

} // namespace IRComponents

#endif /* COMPONENT_LIGHT_SOURCE_H */
