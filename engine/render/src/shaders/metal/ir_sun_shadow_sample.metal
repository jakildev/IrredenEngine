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

#include "ir_sun_shape_query.metal"

#ifndef IR_SUN_FACE_OVERFLOW_REFERENCE
#define IR_SUN_FACE_OVERFLOW_REFERENCE 0
#endif
constant uint kSourceFaceOverflowReferenceBudget = 256u;

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
    float4 sunBasisU; // xyz: sun basis; w: finite caster coverage
    float4 sunBasisV; // xyz: sun basis; w: second raster depth retained
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
    float4 sunCasterViewToWorld;
    int4 fogCeilingEnabled;
};

#include "ir_sun_geometry_storage.metal"

inline float sourceSunGeometrySeparation(device const uint* sunDepthBuf, uint faceIndex, float2 sunUV, float sunZ
#if IR_SUN_QUERY_STATS
    , thread SunQueryStats &sunQueryStats
#endif
) {
    const SourceSunGeometry geometry = readSourceSunGeometry(sunDepthBuf, faceIndex);
#if IR_SUN_QUERY_STATS
    if (geometry.type != 0u) sunQueryStats.counts[kSunQueryShapeCandidates] += 1u;
#endif
    return geometry.type == 0u
        ? sourceFaceRaySeparation(sunUV, sunZ, geometry.corner, geometry.edgeU, geometry.edgeV
#if IR_SUN_QUERY_STATS
            , sunQueryStats
#endif
        )
        : sourceShapeRaySeparation(sunUV, sunZ, geometry.type, geometry.corner, geometry.edgeU, geometry.edgeV);
}

inline bool sourceSunHierarchyBlocks(device const uint* sunDepthBuf, uint capacity, uint leaves, uint records,
    float2 sunUV, float sunZ, float maxShadowThrow
#if IR_SUN_QUERY_STATS
    , thread SunQueryStats &sunQueryStats
#endif
) {
    uint node = 1u;
#if IR_SUN_QUERY_STATS
    uint visited = 0u;
#endif
    while (true) {
        if (sourceSunBoundsOverlap(readSourceSunBounds(sunDepthBuf, capacity, node), sunUV, sunZ,
                                   kShadowBiasQuantNoise, maxShadowThrow)) {
            if (node < leaves) { node *= 2u; continue; }
            const uint first = (node - leaves) * kSourceFaceHierarchyLeafSize;
            for (uint i = first; i < min(records, first + kSourceFaceHierarchyLeafSize); ++i) {
#if IR_SUN_QUERY_STATS
                ++visited;
                sunQueryStats.counts[kSunQueryCandidates] += 1u;
#endif
                const float separation = sourceSunGeometrySeparation(
                    sunDepthBuf, readSourceSunLeafRecord(sunDepthBuf, capacity, i), sunUV, sunZ
#if IR_SUN_QUERY_STATS
                    , sunQueryStats
#endif
                );
                if (separation > kShadowBiasQuantNoise && separation < maxShadowThrow) {
#if IR_SUN_QUERY_STATS
                    sunQueryStats.counts[kSunQueryBlocked] += 1u;
                    sunQueryStats.counts[kSunQueryFirstBlockerVisits] += visited;
#endif
                    return true;
                }
            }
        }
        // Skip completed right subtrees; no fixed traversal budget can certify a miss.
        while (node > 1u && (node & 1u) != 0u) node /= 2u;
        if (node == 1u) break;
        ++node;
    }
#if IR_SUN_QUERY_STATS
    sunQueryStats.counts[kSunQueryExhausted] += 1u;
#endif
    return false;
}

// One stored map word against the receiver: the throw window, then the
// direct / surface near rejection or the coverage-splat same-plane test, which
// reconstructs the write's origin texel from its displacement nibbles and
// rejects an occluder lying in the receiver's own plane. Mirrors GLSL.
inline bool sunStoredTapBlocks(uint stored, float sunZ, float bias, float maxShadowThrow,
                               bool surfaceReceiver, int2 px, float2 origin, float2 texelSz,
                               float2 gradUV, float2 sunUV) {
    if (stored == 0xFFFFFFFFu || (surfaceReceiver && sunWriteIsSurface(stored))) return false;
    float nearestZ = unpackSunDepth(stored);
    // Far shadow-throw window — raw sun-Z gap, on BOTH tap regimes.
    // maxShadowThrow == the feeder / bake sweep (kSunShadowMaxDistance) so
    // a baked caster is receivable at its full throw.
    float depthDiff = sunZ - nearestZ;
    if (depthDiff - bias >= maxShadowThrow) return false;
    if (sunWriteIsDirect(stored) || sunWriteIsSurface(stored)) {
        // Direct and finite surface writes compare depth without splat-origin recovery.
        if (depthDiff <= bias) return false;
    } else {
        int2 offset = unpackSunSplatOffset(stored);
        int2 originTexel = px - offset;
        float2 originUV = origin + (float2(originTexel) + 0.5f) * texelSz;
        float expectedZ = sunZ + dot(gradUV, originUV - sunUV);
        float h = expectedZ - nearestZ;
        if (h <= bias) return false;
    }
    return true;
}

// A nearest word rejected as beyond the throw is the only one that can hide a
// nearer blocker. Mirrors GLSL.
inline bool sunStoredTapBeyondThrow(uint stored, float sunZ, float bias, float maxShadowThrow) {
    return stored != 0xFFFFFFFFu && sunZ - unpackSunDepth(stored) - bias >= maxShadowThrow;
}

// Primary-layer slab extrema behind a nearest word beyond the throw: the slab
// holding the window's far end answers with its max, the next slab with its
// min, each through the same per-word test. Mirrors GLSL.
inline bool sunDepthSlabBlocks(device const uint *sunDepthBuf, int texelIndex, float sunZ, float bias,
                               float maxShadowThrow, bool surfaceReceiver, int2 px, float2 origin,
                               float2 texelSz, float2 gradUV, float2 sunUV) {
    if (kSunDepthSlabSlots == 0u || maxShadowThrow != float(kSunDepthSlabWidthUnits)) return false;
    if ((sunDepthBuf[sunDepthSlabMaxIndex(texelIndex, 0u)] & kSunDepthSlabOverflow) != 0u) return false;
    const uint farSlab = sunDepthSlab(packSunDepth(sunZ - bias - maxShadowThrow, int2(0)));
    for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
        const uint low = sunDepthBuf[sunDepthSlabMinIndex(texelIndex, slot)];
        if (low == 0xFFFFFFFFu) break;
        const uint slab = sunDepthSlab(low);
        if (slab != farSlab && slab != farSlab + 1u) continue;
        // Both extrema of a window slab: the far slab's max and the near slab's
        // min decide depth exactly; the other two recover a splat-rejected extremum.
        if (sunStoredTapBlocks(low, sunZ, bias, maxShadowThrow, surfaceReceiver,
                               px, origin, texelSz, gradUV, sunUV)) return true;
        const uint high = sunDepthBuf[sunDepthSlabMaxIndex(texelIndex, slot)] & ~kSunDepthSlabOverflow;
        if (sunStoredTapBlocks(high, sunZ, bias, maxShadowThrow, surfaceReceiver,
                               px, origin, texelSz, gradUV, sunUV)) return true;
    }
    return false;
}

inline float sampleCascadeShadow(
    float2 sunUV, float sunZ, float3 normal, float3 sunDir, float3 uHat, float3 vHat,
    float2 origin, float2 texelSz, int bufferOffset,
    device const uint *sunDepthBuf, float maxShadowThrow, bool surfaceReceiver,
    bool secondPrimaryDepth, float4 casterViewToWorld, float3 pcfOffset
#if IR_SUN_QUERY_STATS
    , thread SunQueryStats &sunQueryStats
#endif
) {
#if IR_SUN_QUERY_STATS
    sunQueryStats.counts[kSunQueryCascades] += 1u;
#endif
    const uint capacity = sunDepthBuf[kSourceFaceCapacityOffset];
    const uint sourceFaceCount = sunDepthBuf[kSourceFaceHeaderOffset];
    const bool hasSourceFaces = sourceFaceCount != 0u;
    bool sourceQueryComplete = !hasSourceFaces;
    if (surfaceReceiver && hasSourceFaces) {
        const int2 tile = int2(floor((sunUV - origin) / (texelSz * float(kSourceFaceTileEdge))));
        if (all(tile >= int2(0)) && all(tile < int2(kSourceFaceTilesPerAxis))) {
            const uint tileIndex = uint(bufferOffset / kCascadeTexelCount) * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis
                + uint(tile.y) * kSourceFaceTilesPerAxis + uint(tile.x);
            const uint tileBase = sourceFaceTileBase(tileIndex);
            const uint count = sunDepthBuf[tileBase];
            sourceQueryComplete = sourceFaceQueryComplete(count)
                && (count <= kSourceFaceTileCapacity || sunDepthBuf[sourceFaceSpillCounterIndex(capacity)] <= kSourceFaceSpillCapacity);
            const uint retainedFaces = sourceFaceRetainedRecords(sourceFaceCount, capacity);
            const uint hierarchyLeaves = !sourceQueryComplete ? sunDepthBuf[sourceFaceHierarchyReadyIndex(capacity)] : 0u;
            // A tile that lost records answers from the sampled layers: no retained-pool
            // miss can certify it, and a hit-only traversal can visit the whole pool per receiver.
            const bool queryHierarchy = !sourceQueryComplete && sourceFaceTileRetainsRecords(count) &&
                hierarchyLeaves != 0u && hierarchyLeaves == sourceSunHierarchyLeafCount(retainedFaces);
            if (queryHierarchy) {
                sourceQueryComplete = true;
                if (dot(normal, sunDir) > 0.0) {
#if IR_SUN_QUERY_STATS
                    sunQueryStats.counts[kSunQueryIndexed] += 1u;
#endif
                    if (sourceSunHierarchyBlocks(sunDepthBuf, capacity, hierarchyLeaves, retainedFaces,
                                                 sunUV, sunZ, maxShadowThrow
#if IR_SUN_QUERY_STATS
                        , sunQueryStats
#endif
                    )) return 1.0;
                }
            }
            // A complete small record pool is an exact reference for an incomplete tile.
            const bool queryAllFaces = IR_SUN_FACE_OVERFLOW_REFERENCE != 0
                && !sourceQueryComplete && sourceFaceCount <= kSourceFaceOverflowReferenceBudget
                && sourceFaceCount <= capacity;
            sourceQueryComplete = sourceQueryComplete || queryAllFaces;
#if IR_SUN_QUERY_STATS
            if (!sourceQueryComplete) sunQueryStats.counts[kSunQueryIncomplete] += 1u;
#endif
            const uint candidateCount = queryAllFaces ? sourceFaceCount : count;
            if (!queryHierarchy && sourceQueryComplete && dot(normal, sunDir) > 0.0) {
#if IR_SUN_QUERY_STATS
                sunQueryStats.counts[kSunQueryIndexed] += 1u;
#endif
                uint spill = !queryAllFaces && count > kSourceFaceTileCapacity
                    ? sunDepthBuf[sourceFaceSpillHeadIndex(capacity, tileIndex)] : 0xFFFFFFFFu;
                for (uint candidate = 0; candidate < candidateCount; ++candidate) {
#if IR_SUN_QUERY_STATS
                    sunQueryStats.counts[kSunQueryCandidates] += 1u;
#endif
                    uint faceIndex;
                    if (queryAllFaces) {
                        faceIndex = candidate;
                    } else if (candidate < kSourceFaceTileCapacity) {
                        faceIndex = sunDepthBuf[tileBase + 1u + candidate];
                    } else {
                        const uint node = sourceFaceSpillNodeIndex(capacity, spill);
                        faceIndex = sunDepthBuf[node];
                        spill = sunDepthBuf[node + 1u];
                    }
                    const float separation = sourceSunGeometrySeparation(sunDepthBuf, faceIndex, sunUV, sunZ
#if IR_SUN_QUERY_STATS
                        , sunQueryStats
#endif
                    );
                    if (separation > kShadowBiasQuantNoise && separation < maxShadowThrow) {
#if IR_SUN_QUERY_STATS
                        sunQueryStats.counts[kSunQueryBlocked] += 1u;
                        sunQueryStats.counts[kSunQueryFirstBlockerVisits] += candidate + 1u;
#endif
                        return 1.0;
                    }
                }
#if IR_SUN_QUERY_STATS
                sunQueryStats.counts[kSunQueryExhausted] += 1u;
#endif
            }
#if IR_SUN_QUERY_STATS
        } else {
            sunQueryStats.counts[kSunQueryOutside] += 1u;
#endif
        }
    }
    // Finite footprints are rasterized at sun texel centers. Query their cell
    // without blending coverage across its boundary or moving the receiver.
    int2 nearestPixel = int2(floor((sunUV - origin) / texelSz));
    if (surfaceReceiver && nearestPixel.x >= 0 && nearestPixel.y >= 0 &&
        nearestPixel.x < kSunShadowMapDim && nearestPixel.y < kSunShadowMapDim) {
        // Indexed and other casters retain independent depths: rejecting an indexed
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

    // The source-face and nearest-tap queries use the exact receiver; only the
    // canvas-baked coverage layer takes the caller's sun-space offset.
    sunUV += pcfOffset.xy;
    sunZ += pcfOffset.z;

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
            float weight = mix(1.0f - frac.x, frac.x, float(dx))
                         * mix(1.0f - frac.y, frac.y, float(dy));
            // Range and plane rejection are per layer; a nearer invalid tap
            // cannot erase a valid blocker retained in the other layer. The
            // fallback layer holds indexed casters or, without an index, the
            // bake's retained second raster depth. Mirrors GLSL.
            const int layerCount =
                !surfaceReceiver && (hasSourceFaces || secondPrimaryDepth) ? 2 : 1;
            for (int layer = 0; layer < layerCount; ++layer) {
                const int texelIndex = bufferOffset + px.y * kSunShadowMapDim + px.x;
                uint stored = sunDepthBuf[texelIndex + (layer == 1 ? int(kSourceFaceFallbackOffset) : 0)];
                if (!sunStoredTapBlocks(stored, sunZ, bias, maxShadowThrow, surfaceReceiver,
                                        px, origin, texelSz, gradUV, sunUV)) {
                    // Only the primary layer retains what a nearest word beyond the throw hid.
                    if (layer != 0 || (surfaceReceiver && sunWriteIsSurface(stored)) ||
                        !sunStoredTapBeyondThrow(stored, sunZ, bias, maxShadowThrow) ||
                        !sunDepthSlabBlocks(sunDepthBuf, texelIndex, sunZ, bias, maxShadowThrow,
                                            surfaceReceiver, px, origin, texelSz, gradUV, sunUV)) continue;
                }
                shadowAccum += weight;
                break;
            }
        }
    }
    return shadowAccum;
}

// Direct-sun visibility at a world-space surface: 1.0 lit, 0.0 occluded.
// Ambient lighting is composed separately. isoDepth selects/blends cascades.
inline float worldSunShadowFactorImpl(
    float3 pos3D, float3 normal, float isoDepth,
    constant FrameDataSun &sun, device const uint *sunDepthBuf, bool surfaceReceiver, float4 casterViewToWorld,
    float pcfNormalBias
#if IR_SUN_QUERY_STATS
    , thread SunQueryStats &sunQueryStats
#endif
) {
    float3 sunDir = sun.sunDirection.xyz;
    float3 uHat = sun.sunBasisU.xyz;
    float3 vHat = sun.sunBasisV.xyz;
    const float3 pcfOffset = sunSpaceProject(normal * pcfNormalBias, uHat, vHat, sunDir);
    // Shared caster/receiver projection — the bake derives every
    // caster's sun UV + depth from this same function, so cast and receive
    // cannot drift.
    float3 sunProj = sunSpaceProject(
        pos3D, uHat, vHat, sunDir
    );
    float2 sunUV = sunProj.xy;
    float sunZ = sunProj.z;

    int firstCascade = 0;
    int cascadeSamples = 1;
    float blend = 0.0;
    if (sun.cascadeCount > 1) {
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
            firstCascade = 0;
        } else if (!nearInterior || distToSplit > kCascadeBlendRange) {
            firstCascade = 1;
        } else {
            cascadeSamples = 2;
            blend = smoothstep(-kCascadeBlendRange, kCascadeBlendRange, distToSplit);
        }
    }
    // Keep cascade selection at one exact-query call site.
    float shadowAccum = 0.0;
    for (int sampleIndex = 0; sampleIndex < cascadeSamples; ++sampleIndex) {
        const int cascade = firstCascade + sampleIndex;
        float2 origin = sun.sunBufferOriginUV;
        float2 texelSize = sun.sunBufferTexelSize;
        if (sun.cascadeCount > 1) {
            origin = cascade == 0 ? sun.cascadeOriginUV_0 : sun.cascadeOriginUV_1;
            texelSize = cascade == 0 ? sun.cascadeTexelSize_0 : sun.cascadeTexelSize_1;
        }
        const float shadow = sampleCascadeShadow(
            sunUV, sunZ, normal, sunDir, uHat, vHat,
            origin, texelSize, cascade * kCascadeTexelCount, sunDepthBuf, sun.sunMaxShadowThrow, surfaceReceiver,
            sun.sunBasisV.w != 0.0, casterViewToWorld,
            pcfOffset
#if IR_SUN_QUERY_STATS
            , sunQueryStats
#endif
        );
        shadowAccum = sampleIndex == 0 ? shadow : mix(shadowAccum, shadow, blend);
    }
    return 1.0f - shadowAccum;
}

// Raster-origin receivers retain their outward sampling offset.
inline float worldSunShadowFactor(
    float3 pos3D, float3 normal, float isoDepth,
    constant FrameDataSun &sunFrameData, device const uint *sunDepthBuf
#if IR_SUN_QUERY_STATS
    , thread SunQueryStats &sunQueryStats
#endif
) {
    return worldSunShadowFactorImpl(pos3D + normal * kNormalBiasVoxels, normal, isoDepth, sunFrameData, sunDepthBuf, false, float4(0.0, 0.0, 0.0, 1.0), 0.0
#if IR_SUN_QUERY_STATS
        , sunQueryStats
#endif
    );
}

inline float worldSurfaceSunShadowFactor(
    float3 pos3D, float3 normal, float isoDepth, float4 casterViewToWorld,
    constant FrameDataSun &sunFrameData, device const uint *sunDepthBuf
#if IR_SUN_QUERY_STATS
    , thread SunQueryStats &sunQueryStats
#endif
) {
    return worldSunShadowFactorImpl(pos3D, normal, isoDepth, sunFrameData, sunDepthBuf, true, casterViewToWorld, 0.0
#if IR_SUN_QUERY_STATS
        , sunQueryStats
#endif
    );
}

// Finite shape fragments: the canvas-baked coverage splats of the shape's own
// cells are calibrated against the raster receiver's outward offset, so the
// exact fragment position would fail their same-plane test near a face edge.
inline float worldShapeSurfaceSunShadowFactor(
    float3 pos3D, float3 normal, float isoDepth, float4 casterViewToWorld,
    constant FrameDataSun &sunFrameData, device const uint *sunDepthBuf
#if IR_SUN_QUERY_STATS
    , thread SunQueryStats &sunQueryStats
#endif
) {
    return worldSunShadowFactorImpl(pos3D, normal, isoDepth, sunFrameData, sunDepthBuf, true, casterViewToWorld, kNormalBiasVoxels
#if IR_SUN_QUERY_STATS
        , sunQueryStats
#endif
    );
}

#endif // IR_SUN_SHADOW_SAMPLE_METAL_INCLUDED
