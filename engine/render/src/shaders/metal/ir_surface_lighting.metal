#ifndef IR_SURFACE_LIGHTING
#define IR_SURFACE_LIGHTING

#include "ir_tonemap.metal"

// Sun visibility attenuates directional light; indirect fill survives occlusion.
inline float surfaceSunFactor(float ambient, float intensity, float lambert, float visibility) {
    return (ambient + (1.0 - ambient) * lambert * visibility) * intensity;
}

inline float3 surfaceShadowDebugColor(float visibility) {
    return visibility >= 0.999f ? float3(0.0f) : float3(1.0f, 0.0f, 1.0f);
}

struct SurfaceSunTerms {
    float3 ambient;
    float3 direct;
};

// Unoccluded sun contributions; visibility applies only to direct at presentation.
inline SurfaceSunTerms surfaceSunTerms(float3 material, float ambient, float intensity, float lambert) {
    SurfaceSunTerms terms;
    terms.ambient = material * ambient * intensity;
    terms.direct = material * (1.0 - ambient) * lambert * intensity;
    return terms;
}

// Apply exposure and display mapping only after all linear light is composed.
inline float3 surfaceDisplayColor(float3 linear, float exposure, bool hdr) {
    return hdr ? ACESFilm(linear * exposure) : clamp(linear, 0.0, 1.0);
}

// World +Z points down; the upper sky hemisphere is -Z.
inline float3 surfaceSkyLight(float3 worldNormal, float3 skyColor, float intensity, float ao) {
    return skyColor * intensity * max(0.0, -worldNormal.z) * ao;
}

#endif
