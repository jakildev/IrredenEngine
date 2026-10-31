// Shared sun-shadow sampling: the FrameDataSun UBO (binding 29), the baked
// sun-aligned depth-map SSBO (binding 28), and the cascaded PCF lookup. Used by
// BOTH c_compute_sun_shadow (the per-world-pixel screen-space pass) and
// c_lighting_to_trixel (the opt-in detached re-voxelize world-receive path, which
// re-runs the same lookup at a detached voxel's recovered world pos). Deliberately
// NOT in ir_iso_common.glsl, so only the sun-shadow consumers recompile and the
// SDF / voxel / scatter shaders keep their cardinal-yaw byte-identity.
//
// Self-includes ir_sun_projection.glsl — the map-dim constants, the shared
// sunSpaceProject basis, unpackSunDepth, and sunCascadeKernelInterior — so the
// caster bake and this receiver lookup share one source. A wrapper's earlier
// include of ir_sun_projection.glsl makes this a suppressed duplicate.
#include "ir_sun_projection.glsl"
#include "ir_sun_shape_query.glsl"

#ifndef IR_SUN_FACE_OVERFLOW_REFERENCE
#define IR_SUN_FACE_OVERFLOW_REFERENCE 0
#endif
const uint kSourceFaceOverflowReferenceBudget = 256u;

const float kNormalBiasVoxels = 0.5;
const float kShadowBiasTexelScale = 2.0;
const float kShadowBiasSlopeMin = 0.05;
const float kShadowBiasQuantNoise = 4.0 / kSunDepthScale;
const float kCascadeBlendRange = 8.0;

layout(std140, binding = 29) uniform FrameDataSun {
    uniform vec4 sunDirection;
    uniform float sunIntensity;
    uniform float sunAmbient;
    uniform int shadowsEnabled;
    uniform int aoEnabled;
    uniform vec4 sunBasisU; // xyz: sun basis; w: finite caster coverage
    uniform vec4 sunBasisV;
    uniform vec2 sunBufferOriginUV;
    uniform vec2 sunBufferTexelSize;
    uniform vec2 cascadeOriginUV_0;
    uniform vec2 cascadeTexelSize_0;
    uniform vec2 cascadeOriginUV_1;
    uniform vec2 cascadeTexelSize_1;
    uniform float cascadeSplitDepth;
    uniform int cascadeCount;
    uniform float sunSplatMaxTexels;  // unused here (sun-map bake only)
    // Maximum shadow-throw window (sun-Z voxels). BAKE_SUN_SHADOW_MAP sets it
    // from kSunShadowMaxDistance — the SAME distance the feeder / bake AABB
    // sweep uses — so a baked caster is receivable at its full throw and the
    // two cannot drift.
    uniform float sunMaxShadowThrow;
    uniform vec4 sunCasterViewToWorld;
    uniform ivec4 fogCeilingEnabled;
};

layout(std430, binding = 28) readonly buffer SunShadowDepthMap {
    uint sunDepthBuf[];
};

#include "ir_sun_geometry_storage.glsl"

float sourceSunGeometrySeparation(uint faceIndex, vec2 sunUV, float sunZ
#if IR_SUN_QUERY_STATS
    , inout SunQueryStats sunQueryStats
#endif
) {
    const SourceSunGeometry geometry = readSourceSunGeometry(faceIndex);
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

bool sourceSunHierarchyBlocks(uint capacity, uint leaves, uint records,
    vec2 sunUV, float sunZ, float maxShadowThrow
#if IR_SUN_QUERY_STATS
    , inout SunQueryStats sunQueryStats
#endif
) {
    uint node = 1u;
#if IR_SUN_QUERY_STATS
    uint visited = 0u;
#endif
    while (true) {
        if (sourceSunBoundsOverlap(readSourceSunBounds(capacity, node), sunUV, sunZ,
                                   kShadowBiasQuantNoise, maxShadowThrow)) {
            if (node < leaves) { node *= 2u; continue; }
            const uint first = (node - leaves) * kSourceFaceHierarchyLeafSize;
            for (uint i = first; i < min(records, first + kSourceFaceHierarchyLeafSize); ++i) {
#if IR_SUN_QUERY_STATS
                ++visited;
                sunQueryStats.counts[kSunQueryCandidates] += 1u;
#endif
                const float separation = sourceSunGeometrySeparation(i, sunUV, sunZ
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
// direct / surface near rejection or the coverage-splat same-plane test. The
// splat branch reconstructs the write's origin texel from its displacement
// nibbles and tests whether the occluder lies in the receiver's own plane: a
// same-face self-hit extrapolates to h ~ 0 (lit at any splat distance), a
// genuine cast sits a caster height above it (shadow at the base tolerance).
bool sunStoredTapBlocks(uint stored, float sunZ, float bias, float maxShadowThrow,
                        bool surfaceReceiver, ivec2 px, vec2 origin, vec2 texelSz,
                        vec2 gradUV, vec2 sunUV) {
    if (stored == 0xFFFFFFFFu || (surfaceReceiver && sunWriteIsSurface(stored))) return false;
    float nearestZ = unpackSunDepth(stored);
    // Far shadow-throw window — raw sun-Z gap to the stored occluder, on
    // BOTH tap regimes. maxShadowThrow == the feeder / bake sweep
    // (kSunShadowMaxDistance) so a baked caster is receivable at its full
    // throw.
    float depthDiff = sunZ - nearestZ;
    if (depthDiff - bias >= maxShadowThrow) return false;
    if (sunWriteIsDirect(stored) || sunWriteIsSurface(stored)) {
        // Direct writes compare caster depth without splat-plane recovery.
        if (depthDiff <= bias) return false;
    } else {
        ivec2 offset = unpackSunSplatOffset(stored);
        ivec2 originTexel = px - offset;
        vec2 originUV = origin + (vec2(originTexel) + 0.5) * texelSz;
        float expectedZ = sunZ + dot(gradUV, originUV - sunUV);
        float h = expectedZ - nearestZ;
        if (h <= bias) return false;
    }
    return true;
}

// A nearest word rejected as beyond the throw is the only one that can hide a
// nearer blocker; a near or same-plane rejection leaves nothing valid behind it.
bool sunStoredTapBeyondThrow(uint stored, float sunZ, float bias, float maxShadowThrow) {
    return stored != 0xFFFFFFFFu && sunZ - unpackSunDepth(stored) - bias >= maxShadowThrow;
}

// Primary-layer slab extrema behind a nearest word beyond the throw. The window
// (sunZ - bias - throw, sunZ - bias) is one slab wide, so the slab holding its
// far end answers with its max and the next slab with its min; every candidate
// takes the same per-word test, so provenance rejects exactly as a nearest
// word would. An overflowed texel or a throw that differs from the slab
// width answers from the nearest word alone.
bool sunDepthSlabBlocks(int texelIndex, float sunZ, float bias, float maxShadowThrow,
                        bool surfaceReceiver, ivec2 px, vec2 origin, vec2 texelSz,
                        vec2 gradUV, vec2 sunUV) {
    if (kSunDepthSlabSlots == 0u || maxShadowThrow != float(kSunDepthSlabWidthUnits)) return false;
    if ((sunDepthBuf[sunDepthSlabMaxIndex(texelIndex, 0u)] & kSunDepthSlabOverflow) != 0u) return false;
    const uint farSlab = sunDepthSlab(packSunDepth(sunZ - bias - maxShadowThrow, ivec2(0)));
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

float sampleCascadeShadow(
    vec2 sunUV, float sunZ, vec3 normal, vec3 sunDir, vec3 uHat, vec3 vHat,
    vec2 origin, vec2 texelSz, int bufferOffset, float maxShadowThrow, bool surfaceReceiver, vec4 casterViewToWorld,
    vec3 pcfOffset
#if IR_SUN_QUERY_STATS
    , inout SunQueryStats sunQueryStats
#endif
) {
#if IR_SUN_QUERY_STATS
    sunQueryStats.counts[kSunQueryCascades] += 1u;
#endif
    const uint boundWords = uint(sunDepthBuf.length());
    const uint capacity = sourceFaceCapacity(boundWords);
    const uint sourceFaceCount = sunDepthBuf[sourceFaceHeaderIndex(boundWords)];
    const bool hasSourceFaces = sourceFaceCount != 0u;
    bool sourceQueryComplete = !hasSourceFaces;
    if (surfaceReceiver && hasSourceFaces) {
        const ivec2 tile = ivec2(floor((sunUV - origin) / (texelSz * float(kSourceFaceTileEdge))));
        if (all(greaterThanEqual(tile, ivec2(0))) && all(lessThan(tile, ivec2(kSourceFaceTilesPerAxis)))) {
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
                    if (sourceSunHierarchyBlocks(capacity, hierarchyLeaves, retainedFaces,
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
                    const float separation = sourceSunGeometrySeparation(faceIndex, sunUV, sunZ
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
    ivec2 nearestPixel = ivec2(floor((sunUV - origin) / texelSz));
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
                vec3 planeNormal = normal;
                const int casterFace = sunVoxelFaceId(nearest);
                if (casterFace >= 0) {
                    planeNormal = faceOutwardNormal6(casterFace);
                    if (sunVoxelFaceViewAligned(nearest))
                        planeNormal = rotateByQuat(planeNormal, casterViewToWorld);
                }
                vec2 gradient = vec2(dot(planeNormal, uHat), dot(planeNormal, vHat)) / dot(planeNormal, sunDir);
                vec2 tapUV = origin + (vec2(nearestPixel) + 0.5) * texelSz;
                float casterZ = unpackSunDepth(nearest) + dot(gradient, sunUV - tapUV);
                const vec2 receiverGradient = vec2(dot(normal, uHat), dot(normal, vHat)) / facing;
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
    // same-plane test. Derivation: with (uHat, vHat, sunDir) orthonormal
    // and depth z = -dot(P, sunDir), a displacement dP within the receiver plane
    // (dot(dP, normal) = 0) gives dz/du = dot(uHat, normal)/dot(sunDir, normal)
    // and likewise for v; dot(sunDir, normal) is exactly `slope`. So the plane's
    // depth at sun-UV coordinate q is sunZ + dot(gradUV, q - sunUV). (Sign is +:
    // for a coplanar occluder at the write's origin this must reproduce that
    // occluder's own depth, so the splat tap's h is ~0 and it stays lit. The
    // base bias absorbs the sub-texel origin-quantization error, a texel*|grad|
    // term.)
    vec2 gradUV = vec2(dot(normal, uHat), dot(normal, vHat)) / slope;

    // The source-face and nearest-tap queries use the exact receiver; only the
    // canvas-baked coverage layer takes the caller's sun-space offset.
    sunUV += pcfOffset.xy;
    sunZ += pcfOffset.z;

    // Map samples lie at texel centers; integer coordinates address those samples.
    vec2 sunPxF = (sunUV - origin) / texelSz - 0.5;
    ivec2 base = ivec2(floor(sunPxF));
    vec2 frac = sunPxF - vec2(base);
    float shadowAccum = 0.0;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            ivec2 px = base + ivec2(dx, dy);
            if (px.x < 0 || px.x >= kSunShadowMapDim ||
                px.y < 0 || px.y >= kSunShadowMapDim) continue;
            float weight = mix(1.0 - frac.x, frac.x, float(dx))
                         * mix(1.0 - frac.y, frac.y, float(dy));
            // Range and plane rejection are per layer; a nearer invalid tap
            // cannot erase a valid blocker retained in the other layer.
            const int layerCount = !surfaceReceiver && hasSourceFaces ? 2 : 1;
            for (int layer = 0; layer < layerCount; ++layer) {
                const int texelIndex = bufferOffset + px.y * kSunShadowMapDim + px.x;
                uint stored = sunDepthBuf[texelIndex + (layer == 1 ? int(kSourceFaceFallbackOffset) : 0)];
                if (!sunStoredTapBlocks(stored, sunZ, bias, maxShadowThrow, surfaceReceiver,
                                        px, origin, texelSz, gradUV, sunUV)) {
                    // Only the primary layer retains what a nearest word beyond the throw hid.
                    if (layer != 0 || (surfaceReceiver && sunWriteIsSurface(stored)) ||
                        !sunStoredTapBeyondThrow(stored, sunZ, bias, maxShadowThrow) ||
                        !sunDepthSlabBlocks(texelIndex, sunZ, bias, maxShadowThrow, surfaceReceiver,
                                            px, origin, texelSz, gradUV, sunUV)) continue;
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
float worldSunShadowFactorImpl(vec3 pos3D, vec3 normal, float isoDepth, bool surfaceReceiver,
                               vec4 casterViewToWorld, float pcfNormalBias
#if IR_SUN_QUERY_STATS
    , inout SunQueryStats sunQueryStats
#endif
) {
    vec3 sunDir = sunDirection.xyz;
    vec3 uHat = sunBasisU.xyz;
    vec3 vHat = sunBasisV.xyz;
    const vec3 pcfOffset = sunSpaceProject(normal * pcfNormalBias, uHat, vHat, sunDir);
    // Shared caster/receiver projection — the bake derives every
    // caster's sun UV + depth from this same function, so cast and receive
    // cannot drift.
    vec3 sunProj = sunSpaceProject(
        pos3D, uHat, vHat, sunDir
    );
    vec2 sunUV = sunProj.xy;
    float sunZ = sunProj.z;

    int firstCascade = 0;
    int cascadeSamples = 1;
    float blend = 0.0;
    if (cascadeCount > 1) {
        float distToSplit = isoDepth - cascadeSplitDepth;
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
            sunCascadeKernelInterior(sunUV, cascadeOriginUV_0, cascadeTexelSize_0);
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
        vec2 origin = sunBufferOriginUV;
        vec2 texelSize = sunBufferTexelSize;
        if (cascadeCount > 1) {
            origin = cascade == 0 ? cascadeOriginUV_0 : cascadeOriginUV_1;
            texelSize = cascade == 0 ? cascadeTexelSize_0 : cascadeTexelSize_1;
        }
        const float shadow = sampleCascadeShadow(
            sunUV, sunZ, normal, sunDir, uHat, vHat,
            origin, texelSize, cascade * kCascadeTexelCount, sunMaxShadowThrow, surfaceReceiver, casterViewToWorld,
            pcfOffset
#if IR_SUN_QUERY_STATS
            , sunQueryStats
#endif
        );
        shadowAccum = sampleIndex == 0 ? shadow : mix(shadowAccum, shadow, blend);
    }
    return 1.0 - shadowAccum;
}

// Raster-origin receivers retain their outward sampling offset.
float worldSunShadowFactor(vec3 pos3D, vec3 normal, float isoDepth
#if IR_SUN_QUERY_STATS
    , inout SunQueryStats sunQueryStats
#endif
) {
    return worldSunShadowFactorImpl(pos3D + normal * kNormalBiasVoxels, normal, isoDepth, false, vec4(0.0, 0.0, 0.0, 1.0), 0.0
#if IR_SUN_QUERY_STATS
        , sunQueryStats
#endif
    );
}

float worldSurfaceSunShadowFactor(vec3 pos3D, vec3 normal, float isoDepth, vec4 casterViewToWorld
#if IR_SUN_QUERY_STATS
    , inout SunQueryStats sunQueryStats
#endif
) {
    return worldSunShadowFactorImpl(pos3D, normal, isoDepth, true, casterViewToWorld, 0.0
#if IR_SUN_QUERY_STATS
        , sunQueryStats
#endif
    );
}

// Finite shape fragments: the canvas-baked coverage splats of the shape's own
// cells are calibrated against the raster receiver's outward offset, so the
// exact fragment position would fail their same-plane test near a face edge.
float worldShapeSurfaceSunShadowFactor(vec3 pos3D, vec3 normal, float isoDepth, vec4 casterViewToWorld
#if IR_SUN_QUERY_STATS
    , inout SunQueryStats sunQueryStats
#endif
) {
    return worldSunShadowFactorImpl(pos3D, normal, isoDepth, true, casterViewToWorld, kNormalBiasVoxels
#if IR_SUN_QUERY_STATS
        , sunQueryStats
#endif
    );
}
