// Sun-space projection shared by both sides of the sun-shadow pipeline — the caster
// bake (c_bake_sun_shadow_map) and the receiver lookup (ir_sun_shadow_sample) — so
// caster depth and receiver lookup use one basis and cannot drift apart. CPU twin:
// IRMath::sunSpaceProject (the bake driver's cascade-AABB corners); Metal twin:
// metal/ir_sun_projection.metal. Deliberately NOT in ir_iso_common.glsl, so the
// SDF / voxel / scatter shaders that include only that file keep their cardinal-yaw
// byte-identity.
//
// Declares no buffers and no bindings: the bake declares the sun-depth SSBO
// `restrict`, the sample declares it `readonly`; they share only the math here.

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
// casters store and what receivers compare cannot drift.
//
// Bit layout: quantized depth in the high 24 bits, the low BYTE carries the
// coverage-splat DISPLACEMENT VECTOR — a two's-complement nibble each for dx
// (bits [7:4]) and dy (bits [3:0]), the sun-texel offset of THIS write from its
// caster's own texel under the box splat. The radius is capped at
// kSunSplatMaxTexels = 7 (system_bake_sun_shadow_map.hpp) — r7 is the largest
// radius that ROUND-TRIPS: r8 would emit dx = 8, which the nibble aliases to -8
// on unpack, so the receiver would reconstruct the origin texel on the WRONG SIDE
// of the caster. A larger radius needs the displacement field widened past 8
// bits. A DIRECT (caster's-own-texel) write is (dx,dy) = (0,0):
//   - low byte 0 ⇒ the packed word is exactly the quantized depth `<< 8`, so the
//     radius-0 per-axis / smooth-yaw / detached paths carry no splat bits;
//   - atomicMin over the packed word is depth-major (high 24 bits) and, at equal
//     quantized depth, a direct write's 0 low byte beats any splat's nonzero low
//     byte, so a genuine caster's own-texel depth always wins its texel
//     (strengthens the saturated-host invariant — docs/design/sun-shadow-bake-coverage.md).
// Max packed = (2^20 << 8) | 0xFF = 2^28+255 << the 0xFFFFFFFF empty sentinel.
//
// The full displacement vector lets the receiver reconstruct the write's ORIGIN
// texel and run an EXACT same-plane test — rejecting a same-face self-occluder at
// any splat distance while keeping a genuine cast at the base bias
// (ir_sun_shadow_sample), where a widened bias would erode genuine shadows. Why a
// stored vector rather than a widened bias: docs/design/sun-shadow-bake-coverage.md.
uint packSunDepth(float sunZ, ivec2 splatOffset) {
    float biased = clamp(sunZ + kSunDepthOffset, 0.0, kSunDepthOffset * 2.0);
    uint lowByte = (uint(splatOffset.x & 0xF) << 4) | uint(splatOffset.y & 0xF);
    return (uint(biased * kSunDepthScale) << 8) | lowByte;
}

float unpackSunDepth(uint packedDepth) {
    return float(packedDepth >> 8) / kSunDepthScale - kSunDepthOffset;
}

// True iff this sun-map write is a DIRECT caster's-own-texel write (low byte 0),
// vs a coverage-splat neighbour. Direct writes take the receiver's plain
// near-rejection; splat writes take the same-plane test.
bool sunWriteIsDirect(uint packedDepth) {
    return (packedDepth & 0xFFu) == 0u;
}

// The coverage-splat displacement vector (sun texels) this write was splatted
// from its caster's own texel: two sign-extended two's-complement nibbles.
// (0,0) for a direct write. The receiver reconstructs originTexel = px - offset.
ivec2 unpackSunSplatOffset(uint packedDepth) {
    int dx = int((packedDepth >> 4u) & 0xFu);
    int dy = int(packedDepth & 0xFu);
    if (dx >= 8) dx -= 16;
    if (dy >= 8) dy -= 16;
    return ivec2(dx, dy);
}

// May this receiver sample the cascade at (origin, texelSz)? True only where
// its 2x2 PCF kernel sits kSunCascadeInteriorMarginTexels inside the map. Outside,
// the caller must select the covering (outer) cascade instead: a kernel that
// straddles the map edge silently loses the out-of-bounds taps — and near the
// cascade-0 AABB boundary the matching casters may have been bounds-dropped
// by the bake — so edge receivers read a partially-baked region as "lit".
bool sunCascadeKernelInterior(vec2 sunUV, vec2 origin, vec2 texelSz) {
    ivec2 base = ivec2(floor((sunUV - origin) / texelSz));
    return base.x >= kSunCascadeInteriorMarginTexels &&
           base.y >= kSunCascadeInteriorMarginTexels &&
           base.x + 1 < kSunShadowMapDim - kSunCascadeInteriorMarginTexels &&
           base.y + 1 < kSunShadowMapDim - kSunCascadeInteriorMarginTexels;
}
