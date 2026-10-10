#include "ir_surface_lighting.glsl"

vec4 sourceFaceLitColor(vec4 base, vec4 directSunAndExposure,
                         float ao, uint mode, float visibility) {
    if (mode == kSourceLightingAOShadow) {
        const float level = ao * visibility;
        return vec4(level, level, 1.0, base.a);
    }
    if (mode == kSourceLightingShadow) {
        return vec4(surfaceShadowDebugColor(visibility), base.a);
    }
    const vec3 linear = base.rgb + directSunAndExposure.rgb * visibility;
    return vec4(surfaceDisplayColor(linear, directSunAndExposure.w, mode == kSourceLightingHDR), base.a);
}
