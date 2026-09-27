#ifndef IR_PER_AXIS_SHADOW_GLSL_INCLUDED
#define IR_PER_AXIS_SHADOW_GLSL_INCLUDED

// Requires ir_sun_shadow_sample.glsl after its specialization defines.
#include "ir_per_axis_lighting.glsl"

float perAxisSunShadowFactor(vec3 faceOrigin, int faceId) {
    const vec3 center = perAxisFaceCenter(faceOrigin, faceId);
    return worldSurfaceSunShadowFactor(
        center, faceOutwardNormal6(faceId), pos3DtoDistance(center), sunCasterViewToWorld
    );
}

#endif
