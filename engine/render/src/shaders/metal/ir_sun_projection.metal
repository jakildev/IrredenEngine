#ifndef IR_SUN_PROJECTION_METAL_INCLUDED
#define IR_SUN_PROJECTION_METAL_INCLUDED

#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/ir_sun_projection.glsl (#2083) — THE single definition of
// the sun basis math for both the caster bake (c_bake_sun_shadow_map) and the
// receiver lookup (ir_sun_shadow_sample). Extends the shared-distance-basis
// rule (#1923's pos3DtoDistance de-inline) to the sun projection axis: same
// dot-product basis, only the projection axis parameterizes, so caster depth
// and receiver lookup cannot drift apart. CPU twin: IRMath::sunSpaceProject.

constant int kSunShadowMapDim = 1024;
constant int kCascadeTexelCount = kSunShadowMapDim * kSunShadowMapDim;
constant float kSunDepthScale = 1024.0;
constant float kSunDepthOffset = 512.0;

// Interior margin (texels) for sunCascadeKernelInterior. Sized so that when a
// receiver's 2x2 PCF kernel is accepted, its caster — same sun ray, offset in
// UV only by the receiver's kNormalBiasVoxels shift plus the caster's
// half-cell rounding, both sub-texel to low-single-texel at practical texel
// sizes — is guaranteed to have landed inside the map too (the bake's point
// write cannot have been bounds-dropped for an accepted receiver).
constant int kSunCascadeInteriorMarginTexels = 2;

// Sun-space projection of a WORLD point: .xy = UV along the (uHat, vHat)
// orthonormal basis (perpendicular to the sun ray — every caster on a
// receiver's sun ray shares the receiver's UV), .z = depth along the sun ray
// (-sunDir; larger = farther from the sun, packSunDepth's input).
inline float3 sunSpaceProject(float3 pos3D, float3 uHat, float3 vHat, float3 sunDir) {
    return float3(dot(pos3D, uHat), dot(pos3D, vHat), -dot(pos3D, sunDir));
}

// Caster pack / receiver unpack — one co-located inverse pair, so what
// casters store and what receivers compare cannot drift (see #2083).
//
// Bit layout (#2319 splat provenance): quantized depth in the high 24 bits, the
// low BYTE carries the #2270 coverage-splat DISPLACEMENT VECTOR — a
// two's-complement nibble each for dx (bits [7:4]) and dy (bits [3:0]), the
// sun-texel offset of THIS write from its caster's own texel under the box
// splat. The radius is capped at kSunSplatMaxTexels = 6
// (system_bake_sun_shadow_map.hpp), so each component fits the nibble's
// [-8, 7] range. A DIRECT (caster's-own-texel) write is (dx,dy) = (0,0):
//   - low byte 0 ⇒ the recovered depth is bit-exact vs a pure `<< 8` of the
//     pre-#2319 single-write pack, so the radius-0 per-axis / smooth-yaw /
//     detached paths stay byte-identical;
//   - atomic_fetch_min over the packed word is depth-major (high 24 bits) and, at
//     equal quantized depth, a direct write's 0 low byte beats any splat's
//     nonzero low byte, so a genuine caster's own-texel depth always wins its
//     texel (strengthens the saturated-host invariant — docs/design/sun-shadow-bake-coverage.md).
// Max packed = (2^20 << 8) | 0xFF = 2^28+255 << the 0xFFFFFFFF empty sentinel.
//
// The vector (not the scalar displacement of the refuted round-1 form) is what
// lets the receiver reconstruct the write's ORIGIN texel and run an EXACT
// same-plane test — rejecting a same-face self-occluder at any splat distance
// while keeping a genuine cast at the base bias — instead of a widened threshold
// that erodes real cast shadows (ir_sun_shadow_sample; see the design doc).
// Mirrors GLSL.
inline uint packSunDepth(float sunZ, int2 splatOffset) {
    float biased = clamp(sunZ + kSunDepthOffset, 0.0, kSunDepthOffset * 2.0);
    uint lowByte = (uint(splatOffset.x & 0xF) << 4) | uint(splatOffset.y & 0xF);
    return (uint(biased * kSunDepthScale) << 8) | lowByte;
}

inline float unpackSunDepth(uint packedDepth) {
    return float(packedDepth >> 8) / kSunDepthScale - kSunDepthOffset;
}

// True iff this sun-map write is a DIRECT caster's-own-texel write (low byte 0),
// vs a #2270 coverage-splat neighbour. Direct writes keep the receiver's
// unchanged (pre-#2319) near-rejection; splat writes take the same-plane test.
inline bool sunWriteIsDirect(uint packedDepth) {
    return (packedDepth & 0xFFu) == 0u;
}

// The coverage-splat displacement vector (sun texels) this write was splatted
// from its caster's own texel: two sign-extended two's-complement nibbles.
// (0,0) for a direct write. The receiver reconstructs originTexel = px - offset.
inline int2 unpackSunSplatOffset(uint packedDepth) {
    int dx = int((packedDepth >> 4u) & 0xFu);
    int dy = int(packedDepth & 0xFu);
    if (dx >= 8) dx -= 16;
    if (dy >= 8) dy -= 16;
    return int2(dx, dy);
}

// May this receiver sample the cascade at (origin, texelSz)? True only where
// its 2x2 PCF kernel sits interior to the map by the margin above. Outside,
// the caller must select the covering (outer) cascade instead: a kernel that
// straddles the map edge silently loses the out-of-bounds taps — and near the
// cascade-0 AABB boundary the matching casters may have been bounds-dropped
// by the bake — so edge receivers read a partially-baked region as "lit"
// (#2083 root cause 2, the silent-clip face dropout).
inline bool sunCascadeKernelInterior(float2 sunUV, float2 origin, float2 texelSz) {
    int2 base = int2(floor((sunUV - origin) / texelSz));
    return base.x >= kSunCascadeInteriorMarginTexels &&
           base.y >= kSunCascadeInteriorMarginTexels &&
           base.x + 1 < kSunShadowMapDim - kSunCascadeInteriorMarginTexels &&
           base.y + 1 < kSunShadowMapDim - kSunCascadeInteriorMarginTexels;
}

#endif // IR_SUN_PROJECTION_METAL_INCLUDED
