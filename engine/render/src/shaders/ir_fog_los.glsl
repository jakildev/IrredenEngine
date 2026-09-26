// Fog line-of-sight gate — the shader half of the horizon field
// C_CanvasFogOfWar builds on the CPU (component_canvas_fog_of_war.hpp states
// the model). Source i's horizons live at tile row i / 4, channel i % 4, of a
// 256 × 512 RGBA32F image; each tile is anchored on its source's circle
// centre (roundHalfUp(centre) - 128, derived here from the same circle the
// observer block carries); a sample is visible iff its rounded voxel Z is at
// or below (<=, +Z is down) its column's stored horizon. Columns outside a
// source's tile are visible for that source.
//
// Include-FRAGMENT: the wrapper supplies the image slot before including it:
//   #define IR_FOG_LOS_BINDING <slot>
// c_fog_to_trixel binds image 4; the GL probe test binds its own slot.
// Metal twin: metal/ir_fog_los.metal — keep the pure helpers byte-identical
// (FogCrossSectionShaderParity compares them).

// Mirrors kFogLosTileEdge / kFogLosTileHalfExtent / kFogLosSourcesPerTile.
const int kFogLosTileEdge = 256;
const int kFogLosTileHalfExtent = 128;
const int kFogLosSourcesPerTile = 4;

layout(rgba32f, binding = IR_FOG_LOS_BINDING) readonly uniform image2D fogLineOfSight;

bool fogLosSourceGated(int losSourceMask, int source) {
    return ((losSourceMask >> source) & 1) != 0;
}

// The first column of the tile anchored on `circle` (CPU twin:
// FogLineOfSightField::tileOrigin).
ivec2 fogLosTileOrigin(vec4 circle) {
    return roundHalfUp(circle.xy) - ivec2(kFogLosTileHalfExtent);
}

bool fogLosCellInTile(ivec2 cell, ivec2 tileOrigin) {
    const ivec2 local = cell - tileOrigin;
    return local.x >= 0 && local.x < kFogLosTileEdge && local.y >= 0 && local.y < kFogLosTileEdge;
}

ivec2 fogLosTexel(ivec2 cell, ivec2 tileOrigin, int source) {
    const ivec2 local = cell - tileOrigin;
    return ivec2(local.x, local.y + (source / kFogLosSourcesPerTile) * kFogLosTileEdge);
}

float fogLosHorizonChannel(vec4 texel, int source) {
    const int channel = source - (source / kFogLosSourcesPerTile) * kFogLosSourcesPerTile;
    return channel == 0 ? texel.x : (channel == 1 ? texel.y : (channel == 2 ? texel.z : texel.w));
}

bool fogLosSampleVisible(float horizon, int sampleZ) {
    return float(sampleZ) <= horizon;
}

bool fogLosVisible(ivec3 sampleVoxel, int source, vec4 circle) {
    const ivec2 tileOrigin = fogLosTileOrigin(circle);
    if (!fogLosCellInTile(sampleVoxel.xy, tileOrigin)) {
        return true;
    }
    const vec4 texel = imageLoad(fogLineOfSight, fogLosTexel(sampleVoxel.xy, tileOrigin, source));
    return fogLosSampleVisible(fogLosHorizonChannel(texel, source), sampleVoxel.z);
}
