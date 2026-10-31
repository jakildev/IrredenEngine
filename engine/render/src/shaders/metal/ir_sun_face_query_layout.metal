#include "ir_projected_face.metal"
#ifndef IR_SUN_FACE_QUERY_LAYOUT_INCLUDED
#define IR_SUN_FACE_QUERY_LAYOUT_INCLUDED

// SunShadowDepthMap holds depth layers, primary tile lists, geometry and bounded spill lists.
// The record pool and its hierarchy scale with the allocated capacity; every
// word from the spill counter on is addressed through the sourceFace*Index
// helpers. The kSourceFace*Offset constants describe the default allocation
// only.
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
constant uint kSourceFaceCapacityOffset = kSourceFaceHeaderOffset + 1u;
constant uint kSourceFaceHeaderWords = 2u;
constant uint kSourceFaceTileOffset = kSourceFaceHeaderOffset + kSourceFaceHeaderWords;
constant uint kSourceFaceRecordOffset = kSourceFaceTileOffset + kSourceFaceTileCount * (kSourceFaceTileCapacity + 1u);
constant uint kSourceFaceHierarchyLeafSize = 8u;
constant uint kSourceFaceHierarchyNodeWords = 8u;
constant uint kSourceFaceHierarchyWorkgroupSize = 128u;
// Words after the record pool that do not scale with it: the spill counter,
// spill heads and nodes, and the two hierarchy flags.
constant uint kSourceFaceTailFixedWords = 1u + kSourceFaceTileCount + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords + 2u;
// Each record owns ten geometry words and two hierarchy node words.
constant uint kSourceFaceWordsPerRecord = kSourceFaceRecordWords + 2u * kSourceFaceHierarchyNodeWords / kSourceFaceHierarchyLeafSize;
constant uint kSourceFaceSpillCounterOffset = kSourceFaceRecordOffset + kSourceFaceCapacity * kSourceFaceRecordWords;
constant uint kSourceFaceSpillHeadOffset = kSourceFaceSpillCounterOffset + 1u;
constant uint kSourceFaceSpillNodeOffset = kSourceFaceSpillHeadOffset + kSourceFaceTileCount;
constant uint kSourceFaceHierarchyNeededOffset = kSourceFaceSpillNodeOffset + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords;
constant uint kSourceFaceHierarchyReadyOffset = kSourceFaceHierarchyNeededOffset + 1u;
constant uint kSourceFaceHierarchyOffset = kSourceFaceHierarchyReadyOffset + 1u;
constant uint kSourceFaceHierarchyMaxLeaves = kSourceFaceCapacity / kSourceFaceHierarchyLeafSize;
constant uint kSourceFaceBufferWords = kSourceFaceHierarchyOffset + 2u * kSourceFaceHierarchyMaxLeaves * kSourceFaceHierarchyNodeWords;
// Sticky tile-count bit: a failed record reservation left overlapping geometry outside the pool.
constant uint kSourceFaceTileRecordLost = 0x80000000u;

// Kernels read the live record capacity from sunDepthBuf[kSourceFaceCapacityOffset];
// the allocator writes it before the buffer's first dispatch, and the GLSL
// twin derives the same value from the bound length.
inline uint sourceFaceSpillCounterIndex(uint capacity) {
    return kSourceFaceRecordOffset + capacity * kSourceFaceRecordWords;
}

inline uint sourceFaceSpillHeadIndex(uint capacity, uint tileIndex) {
    return sourceFaceSpillCounterIndex(capacity) + 1u + tileIndex;
}

inline uint sourceFaceSpillNodeIndex(uint capacity, uint spill) {
    return sourceFaceSpillCounterIndex(capacity) + 1u + kSourceFaceTileCount + spill * kSourceFaceSpillNodeWords;
}

inline uint sourceFaceHierarchyNeededIndex(uint capacity) {
    return sourceFaceSpillNodeIndex(capacity, kSourceFaceSpillCapacity);
}

inline uint sourceFaceHierarchyReadyIndex(uint capacity) {
    return sourceFaceHierarchyNeededIndex(capacity) + 1u;
}

inline uint sourceFaceHierarchyNodeIndex(uint capacity, uint node) {
    return sourceFaceHierarchyReadyIndex(capacity) + 1u + node * kSourceFaceHierarchyNodeWords;
}

inline uint sourceFaceTileBase(uint tileIndex) {
    return kSourceFaceTileOffset + tileIndex * (kSourceFaceTileCapacity + 1u);
}

inline bool sourceFaceQueryComplete(uint count) {
    return count <= kSourceFaceTileQueryCapacity;
}

inline bool sourceFaceTileRetainsRecords(uint count) {
    return (count & kSourceFaceTileRecordLost) == 0u;
}

// Reservations past capacity publish nothing; every lower ID holds a published record.
inline uint sourceFaceRetainedRecords(uint requested, uint capacity) {
    return requested < capacity ? requested : capacity;
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
