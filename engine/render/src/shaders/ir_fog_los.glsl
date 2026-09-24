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
