#include "ir_sdf_sun_ray.metal"

// Axisymmetric records contain sun-space center, unit symmetry axis, and
// (radius, height or torus minor radius, rendered field threshold).
inline float sourceShapeRaySeparation(float2 sunUV, float sunZ, uint shapeType,
                               float3 center, float3 axis, float3 parameters) {
    float4 params = shapeType == SHAPE_TORUS
        ? float4(parameters.x, parameters.y, 0.0, 0.0)
        : float4(parameters.x, 0.0, parameters.y, 0.0);
    float radius = length(shapeSunHalfExtent(shapeType, params, parameters.z));
    float2 delta = sunUV - center.xy;
    float discriminant = radius * radius - dot(delta, delta);
    if (discriminant < 0.0) return -1.0;
    float nearDepth = center.z - sqrt(discriminant);
    float farDepth = center.z + sqrt(discriminant);
    if (nearDepth >= sunZ) return -1.0;
    float3 perpendicular = normalize(cross(axis, abs(axis.z) < 0.9 ? float3(0, 0, 1) : float3(0, 1, 0)));
    float3 tangent = cross(axis, perpendicular);
    float3 offset = float3(delta, -center.z);
    float3 origin = float3(dot(offset, perpendicular), dot(offset, tangent), dot(offset, axis));
    float3 direction = float3(perpendicular.z, tangent.z, axis.z);
    const float3 halfExtent = shapeSunHalfExtent(shapeType, params, parameters.z);
    for (int coordinate = 0; coordinate < 3; ++coordinate) {
        if (!sdfRaySlab(origin[coordinate], direction[coordinate], -halfExtent[coordinate],
                        halfExtent[coordinate], nearDepth, farDepth)) return -1.0;
    }
    float depth;
    if (!sdfRayHit(shapeType, params, origin, direction, parameters.z, nearDepth, farDepth, depth)) return -1.0;
    return sunZ - depth;
}
