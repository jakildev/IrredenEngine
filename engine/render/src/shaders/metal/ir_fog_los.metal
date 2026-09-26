// Fog line-of-sight gate — Metal twin of ../ir_fog_los.glsl (keep the pure
// helpers byte-identical; FogCrossSectionShaderParity compares them). The
// kernel passes the horizon texture as an argument, so no binding macro is
// needed here. Every function is `static`, so a single include per wrapper
// needs no self-guard.

// Mirrors kFogLosTileEdge / kFogLosTileHalfExtent / kFogLosSourcesPerTile.
constant int kFogLosTileEdge = 256;
constant int kFogLosTileHalfExtent = 128;
constant int kFogLosSourcesPerTile = 4;

static bool fogLosSourceGated(int losSourceMask, int source) {
    return ((losSourceMask >> source) & 1) != 0;
}

// The first column of the tile anchored on `circle` (CPU twin:
// FogLineOfSightField::tileOrigin).
static int2 fogLosTileOrigin(float4 circle) {
    return roundHalfUp(circle.xy) - int2(kFogLosTileHalfExtent);
}

static bool fogLosCellInTile(int2 cell, int2 tileOrigin) {
    const int2 local = cell - tileOrigin;
    return local.x >= 0 && local.x < kFogLosTileEdge && local.y >= 0 && local.y < kFogLosTileEdge;
}

static int2 fogLosTexel(int2 cell, int2 tileOrigin, int source) {
    const int2 local = cell - tileOrigin;
    return int2(local.x, local.y + (source / kFogLosSourcesPerTile) * kFogLosTileEdge);
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
    float4 circle,
    texture2d<float, access::read> fogLineOfSight
) {
    const int2 tileOrigin = fogLosTileOrigin(circle);
    if (!fogLosCellInTile(sampleVoxel.xy, tileOrigin)) {
        return true;
    }
    const float4 texel =
        fogLineOfSight.read(uint2(fogLosTexel(sampleVoxel.xy, tileOrigin, source)));
    return fogLosSampleVisible(fogLosHorizonChannel(texel, source), sampleVoxel.z);
}
