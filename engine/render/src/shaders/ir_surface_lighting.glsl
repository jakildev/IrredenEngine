#ifndef IR_SURFACE_LIGHTING
#define IR_SURFACE_LIGHTING

#include "ir_tonemap.glsl"

// Sun visibility attenuates directional light; indirect fill survives occlusion.
float surfaceSunFactor(float ambient, float intensity, float lambert, float visibility) {
    return (ambient + (1.0 - ambient) * lambert * visibility) * intensity;
}

// Apply exposure and display mapping only after all linear light is composed.
vec3 surfaceDisplayColor(vec3 linear, float exposure, bool hdr) {
    return hdr ? ACESFilm(linear * exposure) : clamp(linear, 0.0, 1.0);
}

// World +Z points down; the upper sky hemisphere is -Z.
vec3 surfaceSkyLight(vec3 worldNormal, vec3 skyColor, float intensity, float ao) {
    return skyColor * intensity * max(0.0, -worldNormal.z) * ao;
}

#endif
