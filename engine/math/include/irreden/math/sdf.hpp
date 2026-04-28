#ifndef IR_MATH_SDF_H
#define IR_MATH_SDF_H

#include <irreden/math/ir_math_types.hpp>

#include <cmath>
#include <cstdint>

namespace IRMath::SDF {

/// Canonical SDF primitive type for the engine. The renderer
/// (`IRRender::ShapeType`) is a `using` alias of this enum, and the GPU shape
/// descriptors store these values directly. Add new shapes here first.
enum class ShapeType : std::uint32_t {
    BOX = 0,
    SPHERE = 1,
    CYLINDER = 2,
    ELLIPSOID = 3,
    CURVED_PANEL = 4,
    WEDGE = 5,
    TAPERED_BOX = 6,
    CUSTOM_SDF = 7,
    CONE = 8,
    TORUS = 9,
};

constexpr float kSurfaceThreshold = 0.5f;

inline vec4 effectiveParams(ShapeType type, vec4 params) {
    if (type != ShapeType::BOX)
        return params;
    return vec4(vec3(params) - vec3(1.0f), params.w);
}

inline float box(vec3 p, vec3 halfExtents) {
    vec3 d = glm::abs(p) - halfExtents;
    return glm::max(d.x, glm::max(d.y, d.z));
}

inline float sphere(vec3 p, float radius) {
    return glm::length(p) - radius;
}

inline float cylinder(vec3 p, float radius, float halfHeight) {
    vec2 d = glm::abs(vec2(glm::length(vec2(p.x, p.y)), p.z)) - vec2(radius, halfHeight);
    return glm::min(glm::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, vec2(0.0f)));
}

inline float ellipsoid(vec3 p, vec3 radii) {
    if (radii.x <= 0.0f || radii.y <= 0.0f || radii.z <= 0.0f)
        return 1.0f;
    float k0 = glm::length(p / radii);
    if (k0 < 1e-6f)
        return -glm::min(radii.x, glm::min(radii.y, radii.z));
    float k1 = glm::length(p / (radii * radii));
    return k0 * (k0 - 1.0f) / k1;
}

inline float taperedBox(vec3 p, vec3 halfExtents, float taper) {
    float taperFactor = glm::mix(
        1.0f,
        taper,
        glm::clamp((p.z + halfExtents.z) / (2.0f * halfExtents.z), 0.0f, 1.0f)
    );
    vec3 scaled = vec3(vec2(p.x, p.y) / glm::max(taperFactor, 0.001f), p.z);
    return box(scaled, halfExtents);
}

inline float cone(vec3 p, float baseRadius, float halfHeight) {
    float t = glm::clamp((p.z + halfHeight) / (2.0f * halfHeight), 0.0f, 1.0f);
    float radiusAtZ = baseRadius * (1.0f - t);
    float dRadial = glm::length(vec2(p.x, p.y)) - radiusAtZ;
    float dZ = std::abs(p.z) - halfHeight;
    float dOutside = glm::length(glm::max(vec2(dRadial, dZ), vec2(0.0f)));
    float dInside = glm::min(glm::max(dRadial, dZ), 0.0f);
    return dOutside + dInside;
}

inline float torus(vec3 p, float majorR, float minorR) {
    float q = glm::length(vec2(p.x, p.y)) - majorR;
    return glm::length(vec2(q, p.z)) - minorR;
}

inline float wedge(vec3 p, vec3 halfExtents) {
    float boxD = box(p, halfExtents);
    float planeD = p.z - halfExtents.z * (1.0f - p.x / glm::max(halfExtents.x, 0.001f));
    return glm::max(boxD, planeD);
}

inline float curvedPanel(vec3 p, vec3 halfExtents, float curvature) {
    float nx = p.x / glm::max(halfExtents.x, 0.001f);
    float zMid = curvature * halfExtents.x * nx * nx;
    float dThickness = std::abs(p.z - zMid) - halfExtents.z;
    float dX = std::abs(p.x) - halfExtents.x;
    float dY = std::abs(p.y) - halfExtents.y;
    float dOutside = glm::length(glm::max(vec3(dX, dY, dThickness), vec3(0.0f)));
    float dInside = glm::min(glm::max(dX, glm::max(dY, dThickness)), 0.0f);
    return dOutside + dInside;
}

inline float evaluate(vec3 localPos, ShapeType type, vec4 params) {
    const vec3 halfSize = vec3(params) * 0.5f;
    switch (type) {
    case ShapeType::BOX:
        return box(localPos, halfSize);
    case ShapeType::SPHERE:
        return sphere(localPos, params.x);
    case ShapeType::CYLINDER:
        return cylinder(localPos, params.x, halfSize.z);
    case ShapeType::ELLIPSOID:
        return ellipsoid(localPos, halfSize);
    case ShapeType::TAPERED_BOX:
        return taperedBox(localPos, halfSize, params.w);
    case ShapeType::CONE:
        return cone(localPos, params.x, halfSize.z);
    case ShapeType::TORUS:
        return torus(localPos, params.x, params.y);
    case ShapeType::WEDGE:
        return wedge(localPos, halfSize);
    case ShapeType::CURVED_PANEL:
        return curvedPanel(localPos, halfSize, params.w);
    default:
        return box(localPos, halfSize);
    }
}

inline vec3 boundingHalf(ShapeType type, vec4 params) {
    switch (type) {
    case ShapeType::SPHERE:
        return vec3(params.x);
    case ShapeType::CYLINDER:
    case ShapeType::CONE:
        return vec3(params.x, params.x, params.z * 0.5f);
    case ShapeType::TORUS: {
        float xyR = params.x + params.y;
        return vec3(xyR, xyR, params.y);
    }
    case ShapeType::CURVED_PANEL: {
        vec3 halfExtents = vec3(params) * 0.5f;
        halfExtents.z += std::abs(params.w) * halfExtents.x;
        return halfExtents;
    }
    default:
        return vec3(params) * 0.5f;
    }
}

inline float boundingRadius(ShapeType type, vec4 params) {
    const vec3 halfExtents = boundingHalf(type, params);
    switch (type) {
    case ShapeType::SPHERE:
        return params.x + 0.5f;
    case ShapeType::CYLINDER:
    case ShapeType::CONE:
        return glm::length(vec2(params.x, halfExtents.z)) + 0.5f;
    case ShapeType::TORUS:
        return params.x + params.y + 0.5f;
    default:
        return glm::length(halfExtents) + std::abs(params.w) + 0.5f;
    }
}

} // namespace IRMath::SDF

#endif /* IR_MATH_SDF_H */
