// Fog line-of-sight gate — Metal twin of ../ir_fog_los.glsl (keep the pure
// helpers byte-identical; FogCrossSectionShaderParity compares them). The
// kernel passes the horizon texture as an argument, so no binding macro is
// needed here. Every function is `static`, so a single include per wrapper
// needs no self-guard.

// Mirrors kFogOfWarSize / kFogOfWarHalfExtent / kFogLosSourcesPerTile.
constant int kFogLosFieldSize = 256;
constant int kFogLosFieldHalfExtent = 128;
constant int kFogLosSourcesPerTile = 4;

static bool fogLosSourceGated(int losSourceMask, int source) {
    return ((losSourceMask >> source) & 1) != 0;
}

static bool fogLosCellInField(int2 cell) {
    return cell.x >= -kFogLosFieldHalfExtent && cell.x < kFogLosFieldHalfExtent &&
        cell.y >= -kFogLosFieldHalfExtent && cell.y < kFogLosFieldHalfExtent;
}

static int2 fogLosTexel(int2 cell, int source) {
    return int2(
        cell.x + kFogLosFieldHalfExtent,
        cell.y + kFogLosFieldHalfExtent + (source / kFogLosSourcesPerTile) * kFogLosFieldSize
    );
}

static float fogLosHorizonChannel(float4 texel, int source) {
    const int channel = source - (source / kFogLosSourcesPerTile) * kFogLosSourcesPerTile;
    return channel == 0 ? texel.x : (channel == 1 ? texel.y : (channel == 2 ? texel.z : texel.w));
}

static bool fogLosSampleVisible(float horizon, int sampleZ) {
    return float(sampleZ) <= horizon;
}

static bool fogLosVisible(
    int3 sampleVoxel,
    int source,
    texture2d<float, access::read> fogLineOfSight
) {
    if (!fogLosCellInField(sampleVoxel.xy)) {
        return true;
    }
    const float4 texel = fogLineOfSight.read(uint2(fogLosTexel(sampleVoxel.xy, source)));
    return fogLosSampleVisible(fogLosHorizonChannel(texel, source), sampleVoxel.z);
}
