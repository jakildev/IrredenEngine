#include "ir_sdf_common.metal"
#include "ir_sdf_ray_roots.metal"

inline float3 shapeSunHalfExtent(uint shapeType, float4 params, float threshold) {
    float3 extent;
    if (shapeType == SHAPE_SPHERE) extent = float3(params.x);
    else if (shapeType == SHAPE_TORUS)
        extent = float3(params.x + params.y, params.x + params.y, params.y);
    else extent = float3(params.x, params.x, params.z * 0.5);
    return extent + float3(threshold);
}
