// Returns incident local light; material multiplication and display mapping belong to composition.
inline float3 surfaceLightVolume(float3 position, int4 volumeOrigin,
                                texture3d<float, access::sample> volume,
                                texture3d<float, access::read> winnerIds,
                                sampler volumeSampler, device const GPULightSource* lights) {
    const float3 localPosition = position - float3(volumeOrigin.xyz);
    const float3 cellPosition = localPosition + float3(kLightVolumeHalfExtent) + float3(0.5);
    const float4 sampleValue = volume.sample(volumeSampler, cellPosition / float3(kLightVolumeSize), level(0.0));
    float3 light = sampleValue.rgb * sampleValue.a;
    if (volumeOrigin.w != 0) {
        const int3 cell = int3(floor(cellPosition));
        if (all(cell >= int3(0)) && all(cell < int3(int(kLightVolumeSize)))) {
            const int winner = roundHalfUp(winnerIds.read(uint3(cell)).r * 255.0);
            if (winner > 0 && int(lights[winner - 1].originAndType.w) == kLightTypeSpot)
                light *= spotConeFactor(lights, winner - 1, position);
        }
    }
    return light;
}
