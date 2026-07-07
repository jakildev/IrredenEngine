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
inline uint packSunDepth(float sunZ) {
    float biased = clamp(sunZ + kSunDepthOffset, 0.0, kSunDepthOffset * 2.0);
    return uint(biased * kSunDepthScale);
}

inline float unpackSunDepth(uint packedDepth) {
    return float(packedDepth) / kSunDepthScale - kSunDepthOffset;
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
