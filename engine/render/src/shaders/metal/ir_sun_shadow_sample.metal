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

#include "ir_sun_face_query_layout.metal"

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
    device const uint *sunDepthBuf, float maxShadowThrow, bool surfaceReceiver, float4 casterViewToWorld
) {
    const bool hasSourceFaces = sunDepthBuf[kSourceFaceHeaderOffset] != 0u;
    bool sourceQueryComplete = !hasSourceFaces;
    if (surfaceReceiver && hasSourceFaces) {
        const int2 tile = int2(floor((sunUV - origin) / (texelSz * float(kSourceFaceTileEdge))));
        if (all(tile >= int2(0)) && all(tile < int2(kSourceFaceTilesPerAxis))) {
            const uint tileIndex = uint(bufferOffset / kCascadeTexelCount) * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis
                + uint(tile.y) * kSourceFaceTilesPerAxis + uint(tile.x);
            const uint tileBase = sourceFaceTileBase(tileIndex);
            const uint count = sunDepthBuf[tileBase];
            sourceQueryComplete = sourceFaceQueryComplete(count);
            if (sourceQueryComplete && dot(normal, sunDir) > 0.0) {
                for (uint candidate = 0; candidate < count; ++candidate) {
                    const uint record = kSourceFaceRecordOffset + kSourceFaceRecordWords * sunDepthBuf[tileBase + 1u + candidate];
                    const float3 corner = float3(as_type<float>(sunDepthBuf[record]), as_type<float>(sunDepthBuf[record+1u]), as_type<float>(sunDepthBuf[record+2u]));
                    const float3 edgeU = float3(as_type<float>(sunDepthBuf[record+3u]), as_type<float>(sunDepthBuf[record+4u]), as_type<float>(sunDepthBuf[record+5u]));
                    const float3 edgeV = float3(as_type<float>(sunDepthBuf[record+6u]), as_type<float>(sunDepthBuf[record+7u]), as_type<float>(sunDepthBuf[record+8u]));
                    const float separation = sourceFaceRaySeparation(sunUV, sunZ, corner, edgeU, edgeV);
                    if (separation > kShadowBiasQuantNoise && separation < maxShadowThrow) return 1.0;
                }
            }
        }
    }
    // Finite footprints are rasterized at sun texel centers. Query their cell
    // without blending coverage across its boundary or moving the receiver.
    int2 nearestPixel = int2(floor((sunUV - origin) / texelSz));
    if (surfaceReceiver && nearestPixel.x >= 0 && nearestPixel.y >= 0 &&
        nearestPixel.x < kSunShadowMapDim && nearestPixel.y < kSunShadowMapDim) {
        // Source and other casters retain independent depths: rejecting a source
        // footprint must not discard another caster hidden behind its map sample.
        for (int layer = 0; layer < 2; ++layer) {
            if (layer == 1 && sourceQueryComplete) continue;
            uint nearest = sunDepthBuf[bufferOffset + nearestPixel.y * kSunShadowMapDim + nearestPixel.x
                + (layer == 1 ? int(kSourceFaceFallbackOffset) : 0)];
            if (sunWriteIsSurface(nearest)) {
                float facing = dot(normal, sunDir);
                if (facing <= 0.0) return 0.0;
                float3 planeNormal = normal;
                const int casterFace = sunVoxelFaceId(nearest);
                if (casterFace >= 0) {
                    planeNormal = faceOutwardNormal6(casterFace);
                    if (sunVoxelFaceViewAligned(nearest))
                        planeNormal = rotateByQuat(planeNormal, casterViewToWorld);
                }
                float2 gradient = float2(dot(planeNormal, uHat), dot(planeNormal, vHat)) / dot(planeNormal, sunDir);
                float2 tapUV = origin + (float2(nearestPixel) + 0.5) * texelSz;
                float casterZ = unpackSunDepth(nearest) + dot(gradient, sunUV - tapUV);
                const float2 receiverGradient = float2(dot(normal, uHat), dot(normal, vHat)) / facing;
                const float receiverSeparation = sunZ + dot(receiverGradient, tapUV - sunUV) - unpackSunDepth(nearest);
                const float casterSeparation = sunZ - casterZ;
                // A frontmost tap alone cannot establish coverage at the receiver.
                // Require front-to-back order on both planes before accepting it.
                if (min(casterSeparation, receiverSeparation) > kShadowBiasQuantNoise &&
                    max(casterSeparation, receiverSeparation) < maxShadowThrow) return 1.0;
            }
        }
    }
    float slope = max(kShadowBiasSlopeMin, dot(normal, sunDir));
    float texelSize = max(texelSz.x, texelSz.y);
    // Receiver tolerance accounts for slope, map resolution and depth quantization.
    float bias = texelSize * kShadowBiasTexelScale / slope + kShadowBiasQuantNoise;

    // Receiver-plane depth gradient in sun-UV, used only by the splat-tap
    // same-plane test below. With (uHat, vHat, sunDir) orthonormal and depth
    // z = -dot(P, sunDir), a displacement within the receiver plane gives
    // dz/du = dot(uHat, normal)/dot(sunDir, normal) (and likewise v), with
    // dot(sunDir, normal) == slope — so the plane's depth at sun-UV q is
    // sunZ + dot(gradUV, q - sunUV). Sign is + (a coplanar occluder at the write
    // origin must reproduce its own depth → h ~ 0 → lit). Mirrors the GLSL twin.
    float2 gradUV = float2(dot(normal, uHat), dot(normal, vHat)) / slope;

    // Map samples lie at texel centers; integer coordinates address those samples.
    float2 sunPxF = (sunUV - origin) / texelSz - 0.5f;
    int2 base = int2(floor(sunPxF));
    float2 frac = sunPxF - float2(base);
    float shadowAccum = 0.0;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            int2 px = base + int2(dx, dy);
            if (px.x < 0 || px.x >= kSunShadowMapDim ||
                px.y < 0 || px.y >= kSunShadowMapDim) continue;
            uint stored = sunDepthBuf[bufferOffset + px.y * kSunShadowMapDim + px.x];
            if (!surfaceReceiver && hasSourceFaces)
                stored = min(stored, sunDepthBuf[kSourceFaceFallbackOffset + bufferOffset + px.y * kSunShadowMapDim + px.x]);
            if (stored == 0xFFFFFFFFu || (surfaceReceiver && sunWriteIsSurface(stored))) continue;
            float nearestZ = unpackSunDepth(stored);
            float weight = mix(1.0f - frac.x, frac.x, float(dx))
                         * mix(1.0f - frac.y, frac.y, float(dy));
            // Far shadow-throw window — raw sun-Z gap, on BOTH tap regimes.
            // maxShadowThrow == the feeder / bake sweep (kSunShadowMaxDistance) so
            // a baked caster is receivable at its full throw. Mirrors the GLSL twin.
            float depthDiff = sunZ - nearestZ;
            if (depthDiff - bias >= maxShadowThrow) continue;

            if (sunWriteIsDirect(stored) || sunWriteIsSurface(stored)) {
                // Direct and finite surface writes compare depth without splat-origin recovery.
                if (depthDiff > bias) shadowAccum += weight;
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
                if (h > bias) shadowAccum += weight;
            }
        }
    }
    return shadowAccum;
}

// Direct-sun visibility at a world-space surface: 1.0 lit, 0.0 occluded.
// Ambient lighting is composed separately. isoDepth selects/blends cascades.
inline float worldSunShadowFactorImpl(
    float3 pos3D, float3 normal, float isoDepth,
    constant FrameDataSun &sun, device const uint *sunDepthBuf, bool surfaceReceiver, float4 casterViewToWorld
) {
    float3 sunDir = sun.sunDirection.xyz;
    float3 uHat = sun.sunBasisU.xyz;
    float3 vHat = sun.sunBasisV.xyz;
    // Shared caster/receiver projection — the bake derives every
    // caster's sun UV + depth from this same function, so cast and receive
    // cannot drift.
    float3 sunProj = sunSpaceProject(
        pos3D, uHat, vHat, sunDir
    );
    float2 sunUV = sunProj.xy;
    float sunZ = sunProj.z;

    float shadowAccum;
    if (sun.cascadeCount <= 1) {
        shadowAccum = sampleCascadeShadow(
            sunUV, sunZ, normal, sunDir, uHat, vHat,
            sun.sunBufferOriginUV, sun.sunBufferTexelSize, 0, sunDepthBuf, sun.sunMaxShadowThrow, surfaceReceiver, casterViewToWorld
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
                sun.cascadeOriginUV_0, sun.cascadeTexelSize_0, 0, sunDepthBuf, sun.sunMaxShadowThrow, surfaceReceiver, casterViewToWorld
            );
        } else if (!nearInterior || distToSplit > kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                sun.cascadeOriginUV_1, sun.cascadeTexelSize_1, kCascadeTexelCount, sunDepthBuf, sun.sunMaxShadowThrow, surfaceReceiver, casterViewToWorld
            );
        } else {
            float nearShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                sun.cascadeOriginUV_0, sun.cascadeTexelSize_0, 0, sunDepthBuf, sun.sunMaxShadowThrow, surfaceReceiver, casterViewToWorld
            );
            float farShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                sun.cascadeOriginUV_1, sun.cascadeTexelSize_1, kCascadeTexelCount, sunDepthBuf, sun.sunMaxShadowThrow, surfaceReceiver, casterViewToWorld
            );
            float t = smoothstep(-kCascadeBlendRange, kCascadeBlendRange, distToSplit);
            shadowAccum = mix(nearShadow, farShadow, t);
        }
    }
    return 1.0f - shadowAccum;
}

// Raster-origin receivers retain their outward sampling offset.
inline float worldSunShadowFactor(
    float3 pos3D, float3 normal, float isoDepth,
    constant FrameDataSun &sunFrameData, device const uint *sunDepthBuf
) {
    return worldSunShadowFactorImpl(pos3D + normal * kNormalBiasVoxels, normal, isoDepth, sunFrameData, sunDepthBuf, false, float4(0.0, 0.0, 0.0, 1.0));
}

inline float worldSurfaceSunShadowFactor(
    float3 pos3D, float3 normal, float isoDepth, float4 casterViewToWorld,
    constant FrameDataSun &sunFrameData, device const uint *sunDepthBuf
) {
    return worldSunShadowFactorImpl(pos3D, normal, isoDepth, sunFrameData, sunDepthBuf, true, casterViewToWorld);
}

#endif // IR_SUN_SHADOW_SAMPLE_METAL_INCLUDED
