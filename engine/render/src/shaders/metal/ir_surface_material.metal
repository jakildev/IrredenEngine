#ifndef IR_SURFACE_MATERIAL_METAL_INCLUDED
#define IR_SURFACE_MATERIAL_METAL_INCLUDED

inline float3 surfaceMaterialColor(
    float3 albedo, float ao, bool usePalette, texture2d<float> paletteLUT
) {
    if (!usePalette) return albedo * ao;
    // AO selects the palette column; unshaded luminance selects its row.
    constexpr sampler paletteSampler(filter::nearest, address::clamp_to_edge);
    const float luminance = dot(albedo, float3(0.299f, 0.587f, 0.114f));
    return albedo * paletteLUT.sample(paletteSampler, float2(ao, luminance), level(0.0)).rgb;
}

#endif
