#include "ir_projected_face.metal"
#ifndef IR_SUN_FACE_QUERY_LAYOUT_INCLUDED
#define IR_SUN_FACE_QUERY_LAYOUT_INCLUDED

// SunShadowDepthMap holds depth layers, primary tile lists, geometry and bounded spill lists.
constant uint kSourceFaceMapDimension = 1024u;
constant uint kSourceFaceCascadeCount = 2u;
constant uint kSourceFaceTileEdge = 8u;
constant uint kSourceFaceTilesPerAxis = kSourceFaceMapDimension / kSourceFaceTileEdge;
constant uint kSourceFaceTileCount = kSourceFaceCascadeCount * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis;
constant uint kSourceFaceTileCapacity = 64u;
constant uint kSourceFaceTileQueryCapacity = 256u;
constant uint kSourceFaceSpillCapacity = 262144u;
constant uint kSourceFaceSpillNodeWords = 2u;
constant uint kSourceFaceCapacity = 65536u;
constant uint kSourceFaceRecordWords = 10u;
constant uint kSourceFaceFallbackOffset = kSourceFaceCascadeCount * kSourceFaceMapDimension * kSourceFaceMapDimension;
constant uint kSourceFaceHeaderOffset = 2u * kSourceFaceFallbackOffset;
constant uint kSourceFaceTileOffset = kSourceFaceHeaderOffset + 1u;
constant uint kSourceFaceRecordOffset = kSourceFaceTileOffset + kSourceFaceTileCount * (kSourceFaceTileCapacity + 1u);
constant uint kSourceFaceSpillCounterOffset = kSourceFaceRecordOffset + kSourceFaceCapacity * kSourceFaceRecordWords;
constant uint kSourceFaceSpillHeadOffset = kSourceFaceSpillCounterOffset + 1u;
constant uint kSourceFaceSpillNodeOffset = kSourceFaceSpillHeadOffset + kSourceFaceTileCount;
constant uint kSourceFaceHierarchyNeededOffset = kSourceFaceSpillNodeOffset + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords;
constant uint kSourceFaceHierarchyReadyOffset = kSourceFaceHierarchyNeededOffset + 1u;
constant uint kSourceFaceHierarchyOffset = kSourceFaceHierarchyReadyOffset + 1u;
constant uint kSourceFaceHierarchyLeafSize = 8u;
constant uint kSourceFaceHierarchyMaxLeaves = kSourceFaceCapacity / kSourceFaceHierarchyLeafSize;
constant uint kSourceFaceHierarchyNodeWords = 8u;
constant uint kSourceFaceBufferWords = kSourceFaceHierarchyOffset + 2u * kSourceFaceHierarchyMaxLeaves * kSourceFaceHierarchyNodeWords;

inline uint sourceFaceTileBase(uint tileIndex) {
    return kSourceFaceTileOffset + tileIndex * (kSourceFaceTileCapacity + 1u);
}

inline bool sourceFaceQueryComplete(uint count) {
    return count <= kSourceFaceTileQueryCapacity;
}

// Positive separation is a front hit, including conservative rounded boundaries.
inline float sourceFaceRaySeparation(float2 sunUV, float sunZ, float3 corner, float3 edgeU, float3 edgeV
#if IR_SUN_QUERY_STATS
    , thread SunQueryStats &sunQueryStats
#endif
) {
    const float determinant = projectedFaceDeterminant(float2(edgeU.x, edgeU.y), float2(edgeV.x, edgeV.y));
    if (abs(determinant) < 0.000001 ||
        !sourceFaceWithinProjectedBounds(sunUV.x, corner.x, edgeU.x, edgeV.x) ||
        !sourceFaceWithinProjectedBounds(sunUV.y, corner.y, edgeU.y, edgeV.y)) {
#if IR_SUN_QUERY_STATS
        sunQueryStats.counts[kSunQueryFaceMisses] += 1u;
#endif
        return -1.0;
    }
    const float2 uv = projectedFaceCoordinates(
        float2(sunUV.x - corner.x, sunUV.y - corner.y),
        float2(edgeU.x, edgeU.y), float2(edgeV.x, edgeV.y), determinant);
    if (!projectedFaceContainsClosed(sunUV, float2(corner.x, corner.y),
            float2(edgeU.x, edgeU.y), float2(edgeV.x, edgeV.y), determinant, uv)) {
#if IR_SUN_QUERY_STATS
        sunQueryStats.counts[kSunQueryFaceMisses] += 1u;
#endif
        return -1.0;
    }
    // An uncertain boundary hit must never extrapolate depth beyond the face.
    const float u = uv.x < 0.0 ? 0.0 : (uv.x > 1.0 ? 1.0 : uv.x);
    const float v = uv.y < 0.0 ? 0.0 : (uv.y > 1.0 ? 1.0 : uv.y);
    return sunZ - (corner.z + u * edgeU.z + v * edgeV.z);
}

#endif
