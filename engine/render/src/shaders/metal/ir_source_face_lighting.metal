#include "ir_tonemap.metal"

float4 sourceFaceLitColor(float4 base, float4 directSunAndExposure,
                         float ao, uint mode, float visibility) {
    if (mode == kSourceLightingAOShadow) {
        const float level = ao * visibility;
        return float4(level, level, 1.0, base.a);
    }
    if (mode == kSourceLightingShadow) {
        return float4(visibility >= 0.999 ? float3(0.0) : float3(1.0, 0.0, 1.0), base.a);
    }
    const float3 linear = base.rgb + directSunAndExposure.rgb * visibility;
    return float4(mode == kSourceLightingHDR
        ? ACESFilm(linear * directSunAndExposure.w) : clamp(linear, 0.0, 1.0), base.a);
}
