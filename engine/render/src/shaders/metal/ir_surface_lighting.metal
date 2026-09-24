#ifndef IR_SURFACE_LIGHTING
#define IR_SURFACE_LIGHTING

#include "ir_tonemap.metal"

// Sun visibility attenuates directional light; indirect fill survives occlusion.
inline float surfaceSunFactor(float ambient, float intensity, float lambert, float visibility) {
    return (ambient + (1.0 - ambient) * lambert * visibility) * intensity;
}

// Apply exposure and display mapping only after all linear light is composed.
inline float3 surfaceDisplayColor(float3 linear, float exposure, bool hdr) {
    return hdr ? ACESFilm(linear * exposure) : clamp(linear, 0.0, 1.0);
}

#endif
