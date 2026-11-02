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
// Per-texel depth-slab extrema of the primary layer; mirrors GLSL.
// Zero compiles the layer out; opt in with the CPU twin (sun_face_query_layout.hpp).
constant uint kSunDepthSlabSlots = 0u;
constant uint kSunDepthSlabWords = 2u;
constant uint kSunDepthSlabTexelWords = kSunDepthSlabSlots * kSunDepthSlabWords;
constant uint kSunDepthSlabOffset = 2u * kSourceFaceFallbackOffset;
constant uint kSunDepthSlabLayerWords = kSourceFaceFallbackOffset * kSunDepthSlabTexelWords;
constant uint kSunDepthSlabWidthUnits = 64u;
// The slab width in packed depth units (width x kSunDepthScale); a literal
// because wrappers include this layout without ir_sun_projection.
constant uint kSunDepthSlabQuantized = 65536u;
constant uint kSunDepthSlabOverflow = 0x80000000u;
// Planar storage: every slot's min plane, then every slot's max plane, so each
// extremum family is one contiguous range a device fill clears.
constant uint kSunDepthSlabPlanesWords = kSunDepthSlabSlots * kSourceFaceFallbackOffset;
constant uint kSunDepthSlabMinPlanesOffset = kSunDepthSlabOffset;
constant uint kSunDepthSlabMaxPlanesOffset = kSunDepthSlabOffset + kSunDepthSlabPlanesWords;
constant uint kSourceFaceHeaderOffset = kSunDepthSlabOffset + kSunDepthSlabLayerWords;
constant uint kSourceFaceCapacityOffset = kSourceFaceHeaderOffset + 1u;
constant uint kSourceFaceHeaderWords = 2u;
constant uint kSourceFaceTileOffset = kSourceFaceHeaderOffset + kSourceFaceHeaderWords;
constant uint kSourceFaceRecordOffset = kSourceFaceTileOffset + kSourceFaceTileCount * (kSourceFaceTileCapacity + 1u);
constant uint kSourceFaceHierarchyLeafSize = 8u;
constant uint kSourceFaceHierarchyNodeWords = 8u;
constant uint kSourceFaceHierarchyWorkgroupSize = 128u;
// Leaf order after the hierarchy nodes: one counting-sort bucket per Morton cell
// of the record bounds' centres, then one record ID per leaf slot. Leaf slot i
// holds record order[i], never record i.
constant uint kSourceFaceHierarchyOrderCells = 16u;
constant uint kSourceFaceHierarchyOrderBuckets = kSourceFaceHierarchyOrderCells * kSourceFaceHierarchyOrderCells * kSourceFaceHierarchyOrderCells;
// hierarchyDispatch.z of the leaf-order passes.
constant uint kSourceFaceHierarchyOrderCount = 1u;
constant uint kSourceFaceHierarchyOrderScan = 2u;
constant uint kSourceFaceHierarchyOrderScatter = 3u;
// Words after the record pool that do not scale with it: the spill counter,
// spill heads and nodes, the two hierarchy flags and the order buckets.
constant uint kSourceFaceTailFixedWords = 1u + kSourceFaceTileCount + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords + 2u + kSourceFaceHierarchyOrderBuckets;
// Each record owns ten geometry words, two hierarchy node words and one leaf slot.
constant uint kSourceFaceWordsPerRecord = kSourceFaceRecordWords + 2u * kSourceFaceHierarchyNodeWords / kSourceFaceHierarchyLeafSize + 1u;
constant uint kSourceFaceSpillCounterOffset = kSourceFaceRecordOffset + kSourceFaceCapacity * kSourceFaceRecordWords;
constant uint kSourceFaceSpillHeadOffset = kSourceFaceSpillCounterOffset + 1u;
constant uint kSourceFaceSpillNodeOffset = kSourceFaceSpillHeadOffset + kSourceFaceTileCount;
constant uint kSourceFaceHierarchyNeededOffset = kSourceFaceSpillNodeOffset + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords;
constant uint kSourceFaceHierarchyReadyOffset = kSourceFaceHierarchyNeededOffset + 1u;
constant uint kSourceFaceHierarchyOffset = kSourceFaceHierarchyReadyOffset + 1u;
constant uint kSourceFaceHierarchyMaxLeaves = kSourceFaceCapacity / kSourceFaceHierarchyLeafSize;
constant uint kSourceFaceHierarchyOrderOffset = kSourceFaceHierarchyOffset + 2u * kSourceFaceHierarchyMaxLeaves * kSourceFaceHierarchyNodeWords;
constant uint kSourceFaceHierarchyOrderEntriesOffset = kSourceFaceHierarchyOrderOffset + kSourceFaceHierarchyOrderBuckets;
constant uint kSourceFaceBufferWords = kSourceFaceHierarchyOrderEntriesOffset + kSourceFaceCapacity;
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

// The order region follows the 2 * capacity / kSourceFaceHierarchyLeafSize hierarchy nodes.
inline uint sourceFaceHierarchyOrderIndex(uint capacity) {
    return sourceFaceHierarchyNodeIndex(capacity, 2u * (capacity / kSourceFaceHierarchyLeafSize));
}

inline uint sourceFaceHierarchyOrderEntriesIndex(uint capacity) {
    return sourceFaceHierarchyOrderIndex(capacity) + kSourceFaceHierarchyOrderBuckets;
}

// Slab of a packed primary word; depth-major, so the splat byte never moves it.
inline uint sunDepthSlab(uint packedDepth) {
    return (packedDepth >> 8) / kSunDepthSlabQuantized;
}

inline uint sunDepthSlabMinIndex(int texelIndex, uint slot) {
    return kSunDepthSlabMinPlanesOffset + slot * kSourceFaceFallbackOffset + uint(texelIndex);
}

inline uint sunDepthSlabMaxIndex(int texelIndex, uint slot) {
    return kSunDepthSlabMaxPlanesOffset + slot * kSourceFaceFallbackOffset + uint(texelIndex);
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
