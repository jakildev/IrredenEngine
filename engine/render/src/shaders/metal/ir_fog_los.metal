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

// Smooth gate — mirror of the GLSL twin: a gated source with softness >= 0
// grades each cell's verdict over a `softness`-voxel band, blends the four
// surrounding cell centres bilinearly for a top-face sample, and takes the
// single column a vertical face looks into. The kernel includes
// ir_iso_common first.

// Mirrors kFogLosHorizonClear (FLT_MAX).
constant float kFogLosHorizonClear = 3.402823466e+38f;

struct FogLosSample {
    int2 cell;
    float2 weight;
    float z;
    bool singleTap;
};

struct FogLosTaps {
    float4 t00;
    float4 t10;
    float4 t01;
    float4 t11;
};

static float fogLosTapVerdict(float horizon, float sampleZ, float softness) {
    if (sampleZ <= horizon) {
        return 1.0f;
    }
    if (softness <= 0.0f) {
        return 0.0f;
    }
    return 1.0f - smoothstep(horizon, horizon + softness, sampleZ);
}

static float fogLosBilinear(float v00, float v10, float v01, float v11, float2 weight) {
    return mix(mix(v00, v10, weight.x), mix(v01, v11, weight.x), weight.y);
}

static float fogLosSmoothVisibility(FogLosTaps taps, int source, FogLosSample s, float softness) {
    return fogLosBilinear(
        fogLosTapVerdict(fogLosHorizonChannel(taps.t00, source), s.z, softness),
        fogLosTapVerdict(fogLosHorizonChannel(taps.t10, source), s.z, softness),
        fogLosTapVerdict(fogLosHorizonChannel(taps.t01, source), s.z, softness),
        fogLosTapVerdict(fogLosHorizonChannel(taps.t11, source), s.z, softness),
        s.weight
    );
}

static FogLosSample fogLosSurfaceSample(float3 pos3D) {
    const float2 base = floor(pos3D.xy);
    FogLosSample s;
    s.cell = int2(base);
    s.weight = pos3D.xy - base;
    s.z = float(roundHalfUp(pos3D.z));
    s.singleTap = false;
    return s;
}

// The line-of-sight voxel whose X- or Y-axis face emitted a single-canvas
// pixel — mirror of the GLSL twin, which states the derivation.
static int3 fogLosFaceVoxel(
    int2 isoRel,
    int rawDepth,
    int viewFaceId,
    int scale,
    bool microFaces,
    int cardinalIndex
) {
    const bool xAxis = (viewFaceId >> 1) == kXFace;
    int3 micro =
        roundHalfUp(isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth)) - float3(-0.5f, 0.0f, 0.5f));
    if (microFaces) {
        const int corner = (xAxis ? micro.x : micro.y) - ((viewFaceId & 1) != 0 ? scale : 0);
        const int phase = corner - scale * int(floor(float(corner) / float(scale)));
        micro = int3(phase) + scale * int3(floor(float3(micro - int3(phase)) / float(scale)));
        if (xAxis) {
            micro.x = corner;
        } else {
            micro.y = corner;
        }
    }
    return roundHalfUp(rotateCardinalZInv(float3(micro) / float(scale), cardinalIndex));
}

static FogLosSample fogLosFaceSample(int3 voxel, int worldFaceId) {
    FogLosSample s;
    s.cell = voxel.xy + faceOutwardNormal6I(worldFaceId).xy;
    s.weight = float2(0.0f);
    s.z = float(voxel.z);
    s.singleTap = true;
    return s;
}

static float4 fogLosTexelOrClear(
    int2 cell,
    int tile,
    texture2d<float, access::read> fogLineOfSight
) {
    if (!fogLosCellInField(cell)) {
        return float4(kFogLosHorizonClear);
    }
    return fogLineOfSight.read(uint2(fogLosTexel(cell, tile * kFogLosSourcesPerTile)));
}

// One tile's taps for sample @p s — four texels, or one for a face sample.
static FogLosTaps fogLosLoadTaps(
    FogLosSample s,
    int tile,
    texture2d<float, access::read> fogLineOfSight
) {
    FogLosTaps taps;
    taps.t00 = fogLosTexelOrClear(s.cell, tile, fogLineOfSight);
    if (s.singleTap) {
        taps.t10 = taps.t00;
        taps.t01 = taps.t00;
        taps.t11 = taps.t00;
        return taps;
    }
    taps.t10 = fogLosTexelOrClear(s.cell + int2(1, 0), tile, fogLineOfSight);
    taps.t01 = fogLosTexelOrClear(s.cell + int2(0, 1), tile, fogLineOfSight);
    taps.t11 = fogLosTexelOrClear(s.cell + int2(1, 1), tile, fogLineOfSight);
    return taps;
}
