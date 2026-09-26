// Fog line-of-sight gate — the shader half of the horizon field
// C_CanvasFogOfWar builds on the CPU (component_canvas_fog_of_war.hpp states
// the model). Source i's horizons live at tile row i / 4, channel i % 4, of a
// 256 × 512 RGBA32F image; a sample is visible iff its rounded voxel Z is at or
// below (<=, +Z is down) its column's stored horizon. Out-of-field columns are
// visible.
//
// Include-FRAGMENT: the wrapper supplies the image slot before including it:
//   #define IR_FOG_LOS_BINDING <slot>
// c_fog_to_trixel binds image 4; the GL probe test binds its own slot.
// Metal twin: metal/ir_fog_los.metal — keep the pure helpers byte-identical
// (FogCrossSectionShaderParity compares them).

// Mirrors kFogOfWarSize / kFogOfWarHalfExtent / kFogLosSourcesPerTile.
const int kFogLosFieldSize = 256;
const int kFogLosFieldHalfExtent = 128;
const int kFogLosSourcesPerTile = 4;

layout(rgba32f, binding = IR_FOG_LOS_BINDING) readonly uniform image2D fogLineOfSight;

bool fogLosSourceGated(int losSourceMask, int source) {
    return ((losSourceMask >> source) & 1) != 0;
}

bool fogLosCellInField(ivec2 cell) {
    return cell.x >= -kFogLosFieldHalfExtent && cell.x < kFogLosFieldHalfExtent &&
        cell.y >= -kFogLosFieldHalfExtent && cell.y < kFogLosFieldHalfExtent;
}

ivec2 fogLosTexel(ivec2 cell, int source) {
    return ivec2(
        cell.x + kFogLosFieldHalfExtent,
        cell.y + kFogLosFieldHalfExtent + (source / kFogLosSourcesPerTile) * kFogLosFieldSize
    );
}

float fogLosHorizonChannel(vec4 texel, int source) {
    const int channel = source - (source / kFogLosSourcesPerTile) * kFogLosSourcesPerTile;
    return channel == 0 ? texel.x : (channel == 1 ? texel.y : (channel == 2 ? texel.z : texel.w));
}

bool fogLosSampleVisible(float horizon, int sampleZ) {
    return float(sampleZ) <= horizon;
}

bool fogLosVisible(ivec3 sampleVoxel, int source) {
    if (!fogLosCellInField(sampleVoxel.xy)) {
        return true;
    }
    const vec4 texel = imageLoad(fogLineOfSight, fogLosTexel(sampleVoxel.xy, source));
    return fogLosSampleVisible(fogLosHorizonChannel(texel, source), sampleVoxel.z);
}

// Smooth gate — a gated source with softness >= 0 (component header states
// the model). Each cell's verdict is graded over a `softness`-voxel band above
// its horizon; a top-face sample blends the four surrounding cell centres'
// verdicts bilinearly at its continuous XY, and a vertical-face sample takes
// the single column its emitting voxel's face looks into. Out-of-field taps
// read clear. The kernel includes ir_iso_common first.

// Mirrors kFogLosHorizonClear (FLT_MAX).
const float kFogLosHorizonClear = 3.402823466e+38;

struct FogLosSample {
    ivec2 cell;
    vec2 weight;
    float z;
    bool singleTap;
};

struct FogLosTaps {
    vec4 t00;
    vec4 t10;
    vec4 t01;
    vec4 t11;
};

float fogLosTapVerdict(float horizon, float sampleZ, float softness) {
    if (sampleZ <= horizon) {
        return 1.0;
    }
    if (softness <= 0.0) {
        return 0.0;
    }
    return 1.0 - smoothstep(horizon, horizon + softness, sampleZ);
}

float fogLosBilinear(float v00, float v10, float v01, float v11, vec2 weight) {
    return mix(mix(v00, v10, weight.x), mix(v01, v11, weight.x), weight.y);
}

float fogLosSmoothVisibility(FogLosTaps taps, int source, FogLosSample s, float softness) {
    return fogLosBilinear(
        fogLosTapVerdict(fogLosHorizonChannel(taps.t00, source), s.z, softness),
        fogLosTapVerdict(fogLosHorizonChannel(taps.t10, source), s.z, softness),
        fogLosTapVerdict(fogLosHorizonChannel(taps.t01, source), s.z, softness),
        fogLosTapVerdict(fogLosHorizonChannel(taps.t11, source), s.z, softness),
        s.weight
    );
}

FogLosSample fogLosSurfaceSample(vec3 pos3D) {
    const vec2 base = floor(pos3D.xy);
    FogLosSample s;
    s.cell = ivec2(base);
    s.weight = pos3D.xy - base;
    s.z = float(roundHalfUp(pos3D.z));
    s.singleTap = false;
    return s;
}

// The line-of-sight voxel (the cell FOG_LOS_BUILD rounds it to) whose X- or
// Y-axis face emitted a single-canvas pixel: the raster inverted from the
// pixel's iso position, raw depth and view face. A face's two pixels sit at
// fixed sub-cell offsets from its micro position — (-2/3, 1/3, 1/3) /
// (-5/6, 1/6, 2/3) on a voxel X face, (-1/6, -1/6, 1/3) / (-1/3, -1/3, 2/3) on
// a voxel Y face, and an SDF face takes the other axis's pair — so
// subtracting (-1/2, 0, 1/2) leaves a residual of at most 1/3 and rounding
// recovers the micro position. With micro faces the plane fixes the voxel's
// lower corner on the face axis (a POS face sits `scale` above it), and the
// in-plane micro offsets snap down onto the same sub-voxel phase; a voxel set
// whose axes sit on different phases recovers the in-plane cell to within one.
// Without micro faces every face is placed at the voxel's own rounded cell.
ivec3 fogLosFaceVoxel(
    ivec2 isoRel,
    int rawDepth,
    int viewFaceId,
    int scale,
    bool microFaces,
    int cardinalIndex
) {
    const bool xAxis = (viewFaceId >> 1) == kXFace;
    ivec3 micro =
        roundHalfUp(isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth)) - vec3(-0.5, 0.0, 0.5));
    if (microFaces) {
        const int corner = (xAxis ? micro.x : micro.y) - ((viewFaceId & 1) != 0 ? scale : 0);
        const int phase = corner - scale * int(floor(float(corner) / float(scale)));
        micro = ivec3(phase) + scale * ivec3(floor(vec3(micro - ivec3(phase)) / float(scale)));
        if (xAxis) {
            micro.x = corner;
        } else {
            micro.y = corner;
        }
    }
    return roundHalfUp(rotateCardinalZInv(vec3(micro) / float(scale), cardinalIndex));
}

FogLosSample fogLosFaceSample(ivec3 voxel, int worldFaceId) {
    FogLosSample s;
    s.cell = voxel.xy + faceOutwardNormal6I(worldFaceId).xy;
    s.weight = vec2(0.0);
    s.z = float(voxel.z);
    s.singleTap = true;
    return s;
}

vec4 fogLosTexelOrClear(ivec2 cell, int tile) {
    if (!fogLosCellInField(cell)) {
        return vec4(kFogLosHorizonClear);
    }
    return imageLoad(fogLineOfSight, fogLosTexel(cell, tile * kFogLosSourcesPerTile));
}

// One tile's taps for sample @p s — four texels, or one for a face sample.
FogLosTaps fogLosLoadTaps(FogLosSample s, int tile) {
    FogLosTaps taps;
    taps.t00 = fogLosTexelOrClear(s.cell, tile);
    if (s.singleTap) {
        taps.t10 = taps.t00;
        taps.t01 = taps.t00;
        taps.t11 = taps.t00;
        return taps;
    }
    taps.t10 = fogLosTexelOrClear(s.cell + ivec2(1, 0), tile);
    taps.t01 = fogLosTexelOrClear(s.cell + ivec2(0, 1), tile);
    taps.t11 = fogLosTexelOrClear(s.cell + ivec2(1, 1), tile);
    return taps;
}
