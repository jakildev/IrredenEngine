#ifndef IR_MATH_SDF_H
#define IR_MATH_SDF_H

#include <irreden/math/ir_math_types.hpp>

#include <cmath>
#include <cstdint>
#include <span>

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

/// Bit-combinable shape flags stored in @c GPUShapeDescriptor::flags. Combine
/// with @c |. Canonical definition lives here so the math-side SDF layer and
/// the render layer share one source of truth; @c IRRender::ShapeFlags is a
/// @c using alias of this type (the @c SHAPE_FLAG_* enumerators are also
/// re-exported into @c IRRender by using-declarations in
/// @c engine/render/include/irreden/render/ir_render_types.hpp).
enum ShapeFlags : std::uint32_t {
    SHAPE_FLAG_NONE = 0,
    SHAPE_FLAG_HOLLOW = 1u << 0, ///< Render only the shell; skip interior voxels.
    SHAPE_FLAG_MIRROR_X = 1u << 1,
    SHAPE_FLAG_MIRROR_Y = 1u << 2,
    SHAPE_FLAG_VISIBLE = 1u << 3,
    SHAPE_FLAG_CHECKERBOARD = 1u << 5,
    /// Color each voxel by its LOCAL iso-depth along the camera's forward axis,
    /// normalized to [0, 1] over the shape's own depth extent. Useful for
    /// visually distinguishing individual shapes regardless of world position.
    SHAPE_FLAG_DEPTH_COLOR = 1u << 6,
    /// X-ray silhouette on occlusion. The shape writes its color normally
    /// where it wins the depth contest; where it loses (something closer
    /// already owns the pixel) `c_shapes_to_trixel` blends the shape color
    /// at reduced alpha over the existing canvas color, so the shape still
    /// reads as a faint silhouette through the occluder. Generic per-shape
    /// opt-in (T-164).
    SHAPE_FLAG_XRAY_OCCLUDED = 1u << 7,
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

/// Batch-evaluate an SDF primitive over a voxel grid centered on the shape
/// origin. Each cell samples at the voxel center
/// (`vec3(x, y, z) + vec3(0.5f) - vec3(size) * 0.5f`); output is laid out
/// to match @ref IRMath::index3DtoIndex1D (x-major, then y, then z).
///
/// The caller must size @p outDistances to at least
/// `size.x * size.y * size.z`; smaller spans are a no-op.
inline void evaluateGrid(ivec3 size, ShapeType type, vec4 params, std::span<float> outDistances) {
    const std::size_t total = static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) *
                              static_cast<std::size_t>(size.z);
    if (outDistances.size() < total)
        return;
    const vec3 center = vec3(size) * 0.5f;
    std::size_t flat = 0;
    for (int z = 0; z < size.z; ++z) {
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                const vec3 sdfPos = vec3(x, y, z) - center + vec3(0.5f);
                outDistances[flat++] = evaluate(sdfPos, type, params);
            }
        }
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
