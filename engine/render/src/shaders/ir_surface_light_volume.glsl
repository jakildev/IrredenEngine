// Winning-light ID volume, image unit 7: `.r` = the index+1 (÷255) of the light that won
// each cell's flood contest. A global, not a parameter: NVIDIA rejects imageLoad on an
// image parameter without a format qualifier, and Mesa rejects `layout` on a parameter.
layout(rgba8, binding = 7) readonly uniform image3D lightVolumeId;

// Returns incident local light; material multiplication and display mapping belong to composition.
vec3 surfaceLightVolume(vec3 position, ivec4 volumeOrigin, sampler3D volume) {
    const vec3 localPosition = position - vec3(volumeOrigin.xyz);
    const vec3 cellPosition = localPosition + vec3(kLightVolumeHalfExtent) + vec3(0.5);
    const vec4 sampleValue = textureLod(volume, cellPosition / vec3(kLightVolumeSize), 0.0);
    vec3 light = sampleValue.rgb * sampleValue.a;
    if (volumeOrigin.w != 0) {
        const ivec3 cell = ivec3(floor(cellPosition));
        if (all(greaterThanEqual(cell, ivec3(0))) && all(lessThan(cell, ivec3(int(kLightVolumeSize))))) {
            const int winner = roundHalfUp(imageLoad(lightVolumeId, cell).r * 255.0);
            if (winner > 0 && int(lights[winner - 1].originAndType.w) == kLightTypeSpot)
                light *= spotConeFactor(winner - 1, position);
        }
    }
    return light;
}
