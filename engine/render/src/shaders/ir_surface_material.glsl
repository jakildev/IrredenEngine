#ifndef IR_SURFACE_MATERIAL_GLSL_INCLUDED
#define IR_SURFACE_MATERIAL_GLSL_INCLUDED

vec3 surfaceMaterialColor(vec3 albedo, float ao, bool usePalette, sampler2D paletteLUT) {
    if (!usePalette) return albedo * ao;
    // AO selects the palette column; unshaded luminance selects its row.
    const float luminance = dot(albedo, vec3(0.299, 0.587, 0.114));
    return albedo * textureLod(paletteLUT, vec2(ao, luminance), 0.0).rgb;
}

#endif
