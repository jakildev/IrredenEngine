// Shared sun-space projection (#2083) — THE single definition of the sun
// basis math for BOTH sides of the sun-shadow pipeline: the caster bake
// (c_bake_sun_shadow_map) and the receiver lookup (ir_sun_shadow_sample).
// Extends the shared-distance-basis rule (#1923's pos3DtoDistance de-inline)
// to the sun projection axis — same dot-product basis, only the projection
// axis parameterizes — so caster depth and receiver lookup cannot drift
// apart. CPU twin: IRMath::sunSpaceProject (the bake driver's cascade-AABB
// corners); Metal twin: metal/ir_sun_projection.metal. Kept in a dedicated
// include — NOT in ir_iso_common.glsl — so the SDF / voxel / scatter shaders
// keep their cardinal-yaw byte-identity (same rationale as
// ir_per_axis_lighting.glsl and ir_sun_shadow_sample.glsl).
//
// Include-order contract: the GLSL include resolver
// (opengl_shader.cpp detail::resolveShaderIncludes) is NON-recursive, so the
// TOP-LEVEL shader must #include this file BEFORE ir_sun_shadow_sample.glsl
// (which consumes these symbols). This file declares no buffers and no
// bindings — the bake declares the sun-depth SSBO `restrict`, the sample
// declares it `readonly`; they share only the math here.

const int kSunShadowMapDim = 1024;
const int kCascadeTexelCount = kSunShadowMapDim * kSunShadowMapDim;
const float kSunDepthScale = 1024.0;
const float kSunDepthOffset = 512.0;

// Interior margin (texels) for sunCascadeKernelInterior. Sized so that when a
// receiver's 2x2 PCF kernel is accepted, its caster — same sun ray, offset in
// UV only by the receiver's kNormalBiasVoxels shift plus the caster's
// half-cell rounding, both sub-texel to low-single-texel at practical texel
// sizes — is guaranteed to have landed inside the map too (the bake's point
// write cannot have been bounds-dropped for an accepted receiver).
const int kSunCascadeInteriorMarginTexels = 2;

// Sun-space projection of a WORLD point: .xy = UV along the (uHat, vHat)
// orthonormal basis (perpendicular to the sun ray — every caster on a
// receiver's sun ray shares the receiver's UV), .z = depth along the sun ray
// (-sunDir; larger = farther from the sun, packSunDepth's input).
vec3 sunSpaceProject(vec3 pos3D, vec3 uHat, vec3 vHat, vec3 sunDir) {
    return vec3(dot(pos3D, uHat), dot(pos3D, vHat), -dot(pos3D, sunDir));
}

// Caster pack / receiver unpack — one co-located inverse pair, so what
// casters store and what receivers compare cannot drift (see #2083).
uint packSunDepth(float sunZ) {
    float biased = clamp(sunZ + kSunDepthOffset, 0.0, kSunDepthOffset * 2.0);
    return uint(biased * kSunDepthScale);
}

float unpackSunDepth(uint packedDepth) {
    return float(packedDepth) / kSunDepthScale - kSunDepthOffset;
}

// May this receiver sample the cascade at (origin, texelSz)? True only where
// its 2x2 PCF kernel sits interior to the map by the margin above. Outside,
// the caller must select the covering (outer) cascade instead: a kernel that
// straddles the map edge silently loses the out-of-bounds taps — and near the
// cascade-0 AABB boundary the matching casters may have been bounds-dropped
// by the bake — so edge receivers read a partially-baked region as "lit"
// (#2083 root cause 2, the silent-clip face dropout).
bool sunCascadeKernelInterior(vec2 sunUV, vec2 origin, vec2 texelSz) {
    ivec2 base = ivec2(floor((sunUV - origin) / texelSz));
    return base.x >= kSunCascadeInteriorMarginTexels &&
           base.y >= kSunCascadeInteriorMarginTexels &&
           base.x + 1 < kSunShadowMapDim - kSunCascadeInteriorMarginTexels &&
           base.y + 1 < kSunShadowMapDim - kSunCascadeInteriorMarginTexels;
}
