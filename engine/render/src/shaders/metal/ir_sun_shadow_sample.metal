#ifndef IR_SUN_SHADOW_SAMPLE_METAL_INCLUDED
#define IR_SUN_SHADOW_SAMPLE_METAL_INCLUDED

// Mirrors shaders/ir_sun_shadow_sample.glsl. Shared sun-shadow sampling — the
// FrameDataSun layout, the cascade PCF sampler, and the world-space
// worldSunShadowFactor() lookup — used by BOTH c_compute_sun_shadow (the
// per-world-pixel screen-space pass) and c_lighting_to_trixel (the opt-in
// detached re-voxelize world-receive path). On Metal the
// sun-depth map (buffer 28) is a kernel argument, so it threads through as a
// `device const uint *` parameter rather than a global SSBO.

// Map-dim constants, the shared sunSpaceProject basis, unpackSunDepth, and
// sunCascadeKernelInterior — one source with the caster bake.
#include "ir_sun_projection.metal"

constant float kShadowDarken = 0.45;
constant float kNormalBiasVoxels = 0.5;
constant float kShadowBiasTexelScale = 2.0;
constant float kShadowBiasSlopeMin = 0.05;
constant float kShadowBiasQuantNoise = 4.0 / kSunDepthScale;
constant float kCascadeBlendRange = 8.0;

struct FrameDataSun {
    float4 sunDirection;
    float sunIntensity;
    float sunAmbient;
    int shadowsEnabled;
    int aoEnabled;
    float4 sunBasisU;
    float4 sunBasisV;
    float2 sunBufferOriginUV;
    float2 sunBufferTexelSize;
    float2 cascadeOriginUV_0;
    float2 cascadeTexelSize_0;
    float2 cascadeOriginUV_1;
    float2 cascadeTexelSize_1;
    float cascadeSplitDepth;
    int cascadeCount;
    float sunSplatMaxTexels;  // unused here (sun-map bake only)
    // Maximum shadow-throw window (sun-Z voxels). BAKE_SUN_SHADOW_MAP sets it
    // from kSunShadowMaxDistance — the SAME distance the feeder / bake AABB
    // sweep uses — so a baked caster is receivable at its full throw and the
    // two cannot drift.
    float sunMaxShadowThrow;
};

inline float sampleCascadeShadow(
    float2 sunUV, float sunZ, float3 normal, float3 sunDir, float3 uHat, float3 vHat,
    float2 origin, float2 texelSz, int bufferOffset,
    device const uint *sunDepthBuf, float maxShadowThrow, float selfStepDepthRange = 0.0
) {
    float slope = max(kShadowBiasSlopeMin, dot(normal, sunDir));
    float texelSize = max(texelSz.x, texelSz.y);
    // Base receiver near-rejection — the trustworthy tolerance for a DIRECT
    // (caster's-own-texel) sun-map write. selfStepDepthRange (0 except on a
    // detected round-to-cell staircase riser) lifts it. Both tap regimes below use
    // this same base with NO per-tap widening: a global bias cannot separate a
    // same-face self-occluder from a real cast occluder, so widening it erodes
    // genuine cast shadows (docs/design/sun-shadow-bake-coverage.md). The far
    // shadow-throw window (maxShadowThrow) also uses this base `bias`. Mirrors the GLSL twin.
    float bias = texelSize * kShadowBiasTexelScale / slope + kShadowBiasQuantNoise;
    float nearReject = max(bias, selfStepDepthRange);

    // Receiver-plane depth gradient in sun-UV, used only by the splat-tap
    // same-plane test below. With (uHat, vHat, sunDir) orthonormal and depth
    // z = -dot(P, sunDir), a displacement within the receiver plane gives
    // dz/du = dot(uHat, normal)/dot(sunDir, normal) (and likewise v), with
    // dot(sunDir, normal) == slope — so the plane's depth at sun-UV q is
    // sunZ + dot(gradUV, q - sunUV). Sign is + (a coplanar occluder at the write
    // origin must reproduce its own depth → h ~ 0 → lit). Mirrors the GLSL twin.
    float2 gradUV = float2(dot(normal, uHat), dot(normal, vHat)) / slope;

    float2 sunPxF = (sunUV - origin) / texelSz;
    int2 base = int2(floor(sunPxF));
    float2 frac = sunPxF - float2(base);
    float shadowAccum = 0.0;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            int2 px = base + int2(dx, dy);
            if (px.x < 0 || px.x >= kSunShadowMapDim ||
                px.y < 0 || px.y >= kSunShadowMapDim) continue;
            uint stored = sunDepthBuf[bufferOffset + px.y * kSunShadowMapDim + px.x];
            if (stored == 0xFFFFFFFFu) continue;
            float nearestZ = unpackSunDepth(stored);
            float weight = mix(1.0f - frac.x, frac.x, float(dx))
                         * mix(1.0f - frac.y, frac.y, float(dy));
            // Far shadow-throw window — raw sun-Z gap, on BOTH tap regimes.
            // maxShadowThrow == the feeder / bake sweep (kSunShadowMaxDistance) so
            // a baked caster is receivable at its full throw. Mirrors the GLSL twin.
            float depthDiff = sunZ - nearestZ;
            if (depthDiff - bias >= maxShadowThrow) continue;

            if (sunWriteIsDirect(stored)) {
                // DIRECT caster's-own-texel write — plain near-rejection. A radius-0
                // bake writes only direct texels, so it takes this branch alone.
                if (depthDiff > nearReject) shadowAccum += weight;
            } else {
                // Coverage-SPLAT neighbour: reconstruct the write's origin
                // and reject an occluder that lies in the RECEIVER's own plane
                // (a same-face self-hit → h ~ 0 → lit at any splat distance); a
                // genuine cast sits far above the plane (h ~ caster height → shadow
                // at the base tolerance, no widening → no erosion). Mirrors GLSL.
                int2 offset = unpackSunSplatOffset(stored);
                int2 originTexel = px - offset;
                float2 originUV = origin + (float2(originTexel) + 0.5f) * texelSz;
                float expectedZ = sunZ + dot(gradUV, originUV - sunUV);
                float h = expectedZ - nearestZ;
                if (h > nearReject) shadowAccum += weight;
            }
        }
    }
    return shadowAccum;
}

// Per-surface sun-shadow brightness factor for a WORLD-space surface point +
// world normal, selecting the cascade by iso depth and blending across the
// split. Returns 1.0 (lit) … kShadowDarken (shadowed). Shared by
// c_compute_sun_shadow and the detached world-receive path.
inline float worldSunShadowFactor(
    float3 pos3D, float3 normal, float isoDepth,
    constant FrameDataSun &sun, device const uint *sunDepthBuf,
    float selfStepDepthRange = 0.0
) {
    float3 sunDir = sun.sunDirection.xyz;
    float3 uHat = sun.sunBasisU.xyz;
    float3 vHat = sun.sunBasisV.xyz;
    // Shared caster/receiver projection — the bake derives every
    // caster's sun UV + depth from this same function, so cast and receive
    // cannot drift.
    float3 sunProj = sunSpaceProject(
        pos3D + normal * kNormalBiasVoxels, uHat, vHat, sunDir
    );
    float2 sunUV = sunProj.xy;
    float sunZ = sunProj.z;

    // selfStepDepthRange (0 = no lift) is threaded to every cascade sample so the
    // staircase self-step carve applies in whichever cascade the receiver lands
    // in. Callers with no per-receiver staircase signal take the default 0.0.
    float shadowAccum;
    if (sun.cascadeCount <= 1) {
        shadowAccum = sampleCascadeShadow(
            sunUV, sunZ, normal, sunDir, uHat, vHat,
            sun.sunBufferOriginUV, sun.sunBufferTexelSize, 0, sunDepthBuf, sun.sunMaxShadowThrow, selfStepDepthRange
        );
    } else {
        float distToSplit = isoDepth - sun.cascadeSplitDepth;
        // Covering-cascade fallback: the near cascade is valid for
        // this receiver only where its PCF kernel sits interior to the map —
        // its AABB was built from a depth-capped corner set, so a receiver
        // near the map edge (screen corners, the split blend band past the
        // cap) can straddle a region where taps fall out of bounds and the
        // matching casters were bounds-dropped by the bake. Selecting the
        // covering far cascade there trades texel resolution for a complete
        // kernel instead of silently reading the missing region as "lit"
        // (partial face dropout). The gate is per-receiver-UV, uniform across
        // a voxel's faces, so a straddling voxel's faces select consistently.
        bool nearInterior =
            sunCascadeKernelInterior(sunUV, sun.cascadeOriginUV_0, sun.cascadeTexelSize_0);
        if (nearInterior && distToSplit < -kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                sun.cascadeOriginUV_0, sun.cascadeTexelSize_0, 0, sunDepthBuf, sun.sunMaxShadowThrow, selfStepDepthRange
            );
        } else if (!nearInterior || distToSplit > kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                sun.cascadeOriginUV_1, sun.cascadeTexelSize_1, kCascadeTexelCount, sunDepthBuf, sun.sunMaxShadowThrow, selfStepDepthRange
            );
        } else {
            float nearShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                sun.cascadeOriginUV_0, sun.cascadeTexelSize_0, 0, sunDepthBuf, sun.sunMaxShadowThrow, selfStepDepthRange
            );
            float farShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                sun.cascadeOriginUV_1, sun.cascadeTexelSize_1, kCascadeTexelCount, sunDepthBuf, sun.sunMaxShadowThrow, selfStepDepthRange
            );
            float t = smoothstep(-kCascadeBlendRange, kCascadeBlendRange, distToSplit);
            shadowAccum = mix(nearShadow, farShadow, t);
        }
    }
    return mix(1.0f, kShadowDarken, shadowAccum);
}

#endif // IR_SUN_SHADOW_SAMPLE_METAL_INCLUDED
