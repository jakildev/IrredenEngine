#include "ir_sdf_common.glsl"
#include "ir_sdf_ray_roots.glsl"

vec3 shapeSunHalfExtent(uint shapeType, vec4 params, float threshold) {
    vec3 extent;
    if (shapeType == SHAPE_SPHERE) extent = vec3(params.x);
    else if (shapeType == SHAPE_TORUS)
        extent = vec3(params.x + params.y, params.x + params.y, params.y);
    else extent = vec3(params.x, params.x, params.z * 0.5);
    return extent + vec3(threshold);
}
