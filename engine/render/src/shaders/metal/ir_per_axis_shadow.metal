#ifndef IR_PER_AXIS_SHADOW_METAL_INCLUDED
#define IR_PER_AXIS_SHADOW_METAL_INCLUDED

// Requires ir_sun_shadow_sample.metal after its specialization defines.
#include "ir_per_axis_lighting.metal"

inline float perAxisSunShadowFactor(
    float3 faceOrigin, int faceId,
    constant FrameDataSun &sun, device const uint *sunDepthBuf
) {
    const float3 center = perAxisFaceCenter(faceOrigin, faceId);
    return worldSurfaceSunShadowFactor(
        center, faceOutwardNormal6(faceId), pos3DtoDistance(center),
        sun.sunCasterViewToWorld, sun, sunDepthBuf
    );
}

#endif
