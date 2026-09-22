#include "ir_tonemap.glsl"

vec4 sourceFaceLitColor(vec4 base, vec4 directSunAndExposure,
                         float ao, uint mode, float visibility) {
    if (mode == kSourceLightingAOShadow) {
        const float level = ao * visibility;
        return vec4(level, level, 1.0, base.a);
    }
    if (mode == kSourceLightingShadow) {
        return vec4(visibility >= 0.999 ? vec3(0.0) : vec3(1.0, 0.0, 1.0), base.a);
    }
    const vec3 linear = base.rgb + directSunAndExposure.rgb * visibility;
    return vec4(mode == kSourceLightingHDR
        ? ACESFilm(linear * directSunAndExposure.w) : clamp(linear, 0.0, 1.0), base.a);
}
