#include "ir_sdf_sun_ray.glsl"

// Axisymmetric records contain sun-space center, unit symmetry axis, and
// (radius, height or torus minor radius, rendered field threshold).
float sourceShapeRaySeparation(vec2 sunUV, float sunZ, uint shapeType,
                               vec3 center, vec3 axis, vec3 parameters) {
    vec4 params = shapeType == SHAPE_TORUS
        ? vec4(parameters.x, parameters.y, 0.0, 0.0)
        : vec4(parameters.x, 0.0, parameters.y, 0.0);
    float radius = length(shapeSunHalfExtent(shapeType, params, parameters.z));
    vec2 delta = sunUV - center.xy;
    float discriminant = radius * radius - dot(delta, delta);
    if (discriminant < 0.0) return -1.0;
    float nearDepth = center.z - sqrt(discriminant);
    float farDepth = center.z + sqrt(discriminant);
    if (nearDepth >= sunZ) return -1.0;
    vec3 perpendicular = normalize(cross(axis, abs(axis.z) < 0.9 ? vec3(0, 0, 1) : vec3(0, 1, 0)));
    vec3 tangent = cross(axis, perpendicular);
    vec3 offset = vec3(delta, -center.z);
    vec3 origin = vec3(dot(offset, perpendicular), dot(offset, tangent), dot(offset, axis));
    vec3 direction = vec3(perpendicular.z, tangent.z, axis.z);
    const vec3 halfExtent = shapeSunHalfExtent(shapeType, params, parameters.z);
    for (int coordinate = 0; coordinate < 3; ++coordinate) {
        if (!sdfRaySlab(origin[coordinate], direction[coordinate], -halfExtent[coordinate],
                        halfExtent[coordinate], nearDepth, farDepth)) return -1.0;
    }
    float depth;
    if (!sdfRayHit(shapeType, params, origin, direction, parameters.z, nearDepth, farDepth, depth)) return -1.0;
    return sunZ - depth;
}
