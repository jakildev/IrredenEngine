#include "ir_projected_face.glsl"
#ifndef IR_SUN_FACE_QUERY_LAYOUT_INCLUDED
#define IR_SUN_FACE_QUERY_LAYOUT_INCLUDED

// SunShadowDepthMap holds depth layers, primary tile lists, geometry and bounded spill lists.
// The record pool and its hierarchy scale with the allocated capacity; every
// word from the spill counter on is addressed through the sourceFace*Index
// helpers. The kSourceFace*Offset constants describe the default allocation
// only.
const uint kSourceFaceMapDimension = 1024u;
const uint kSourceFaceCascadeCount = 2u;
const uint kSourceFaceTileEdge = 8u;
const uint kSourceFaceTilesPerAxis = kSourceFaceMapDimension / kSourceFaceTileEdge;
const uint kSourceFaceTileCount = kSourceFaceCascadeCount * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis;
const uint kSourceFaceTileCapacity = 64u;
const uint kSourceFaceTileQueryCapacity = 256u;
const uint kSourceFaceSpillCapacity = 262144u;
const uint kSourceFaceSpillNodeWords = 2u;
const uint kSourceFaceCapacity = 65536u;
const uint kSourceFaceRecordWords = 10u;
const uint kSourceFaceFallbackOffset = kSourceFaceCascadeCount * kSourceFaceMapDimension * kSourceFaceMapDimension;
// Per-texel depth-slab extrema of the primary layer: kSunDepthSlabSlots slots
// of (min, max) packed words, each slot owning one slab of kSunDepthSlabWidthUnits
// depth units. The slab width equals the receiver's shadow throw, so a
// throw-wide window (a, a + width) is answered exactly by the max of the slab
// holding a and the min of the next slab. Slot 0's max word carries the
// sticky overflow bit once a texel saw more distinct slabs than it has slots.
// Zero compiles the layer out; opt in with the CPU twin (sun_face_query_layout.hpp).
const uint kSunDepthSlabSlots = 0u;
const uint kSunDepthSlabWords = 2u;
const uint kSunDepthSlabTexelWords = kSunDepthSlabSlots * kSunDepthSlabWords;
const uint kSunDepthSlabOffset = 2u * kSourceFaceFallbackOffset;
const uint kSunDepthSlabLayerWords = kSourceFaceFallbackOffset * kSunDepthSlabTexelWords;
const uint kSunDepthSlabWidthUnits = 64u;
// The slab width in packed depth units (width x kSunDepthScale); a literal
// because wrappers include this layout without ir_sun_projection.
const uint kSunDepthSlabQuantized = 65536u;
const uint kSunDepthSlabOverflow = 0x80000000u;
const uint kSourceFaceHeaderOffset = kSunDepthSlabOffset + kSunDepthSlabLayerWords;
const uint kSourceFaceCapacityOffset = kSourceFaceHeaderOffset + 1u;
const uint kSourceFaceHeaderWords = 2u;
const uint kSourceFaceTileOffset = kSourceFaceHeaderOffset + kSourceFaceHeaderWords;
const uint kSourceFaceRecordOffset = kSourceFaceTileOffset + kSourceFaceTileCount * (kSourceFaceTileCapacity + 1u);
const uint kSourceFaceHierarchyLeafSize = 8u;
const uint kSourceFaceHierarchyNodeWords = 8u;
const uint kSourceFaceHierarchyWorkgroupSize = 128u;
// Leaf order after the hierarchy nodes: one counting-sort bucket per Morton cell
// of the record bounds' centres, then one record ID per leaf slot. Leaf slot i
// holds record order[i], never record i.
const uint kSourceFaceHierarchyOrderCells = 16u;
const uint kSourceFaceHierarchyOrderBuckets = kSourceFaceHierarchyOrderCells * kSourceFaceHierarchyOrderCells * kSourceFaceHierarchyOrderCells;
// hierarchyDispatch.z of the leaf-order passes.
const uint kSourceFaceHierarchyOrderCount = 1u;
const uint kSourceFaceHierarchyOrderScan = 2u;
const uint kSourceFaceHierarchyOrderScatter = 3u;
// Words after the record pool that do not scale with it: the spill counter,
// spill heads and nodes, the two hierarchy flags and the order buckets.
const uint kSourceFaceTailFixedWords = 1u + kSourceFaceTileCount + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords + 2u + kSourceFaceHierarchyOrderBuckets;
// Each record owns ten geometry words, two hierarchy node words and one leaf slot.
const uint kSourceFaceWordsPerRecord = kSourceFaceRecordWords + 2u * kSourceFaceHierarchyNodeWords / kSourceFaceHierarchyLeafSize + 1u;
const uint kSourceFaceSpillCounterOffset = kSourceFaceRecordOffset + kSourceFaceCapacity * kSourceFaceRecordWords;
const uint kSourceFaceSpillHeadOffset = kSourceFaceSpillCounterOffset + 1u;
const uint kSourceFaceSpillNodeOffset = kSourceFaceSpillHeadOffset + kSourceFaceTileCount;
const uint kSourceFaceHierarchyNeededOffset = kSourceFaceSpillNodeOffset + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords;
const uint kSourceFaceHierarchyReadyOffset = kSourceFaceHierarchyNeededOffset + 1u;
const uint kSourceFaceHierarchyOffset = kSourceFaceHierarchyReadyOffset + 1u;
const uint kSourceFaceHierarchyMaxLeaves = kSourceFaceCapacity / kSourceFaceHierarchyLeafSize;
const uint kSourceFaceHierarchyOrderOffset = kSourceFaceHierarchyOffset + 2u * kSourceFaceHierarchyMaxLeaves * kSourceFaceHierarchyNodeWords;
const uint kSourceFaceHierarchyOrderEntriesOffset = kSourceFaceHierarchyOrderOffset + kSourceFaceHierarchyOrderBuckets;
const uint kSourceFaceBufferWords = kSourceFaceHierarchyOrderEntriesOffset + kSourceFaceCapacity;
// Sticky tile-count bit: a failed record reservation left overlapping geometry outside the pool.
const uint kSourceFaceTileRecordLost = 0x80000000u;

// Every header access passes `uint(sunDepthBuf.length())`: NVIDIA's cold link
// time grows with a compile-time-constant SSBO index, and this one costs minutes
// per program, while a runtime-derived index links in well under a second.
// The bound length also carries the allocated record capacity, which the
// Metal twin reads from the header word instead. Addresses the header only
// while binding 28 holds the whole SunShadowDepthMap; a bindRange, a size not
// produced by the layout, or another buffer aliased onto slot 28 misaddresses it.
uint sourceFaceCapacity(uint boundWords) {
    return (boundWords - kSourceFaceRecordOffset - kSourceFaceTailFixedWords) / kSourceFaceWordsPerRecord;
}

uint sourceFaceHeaderIndex(uint boundWords) {
    return boundWords - (kSourceFaceRecordOffset - kSourceFaceHeaderOffset) - kSourceFaceTailFixedWords
        - sourceFaceCapacity(boundWords) * kSourceFaceWordsPerRecord;
}

uint sourceFaceSpillCounterIndex(uint capacity) {
    return kSourceFaceRecordOffset + capacity * kSourceFaceRecordWords;
}

uint sourceFaceSpillHeadIndex(uint capacity, uint tileIndex) {
    return sourceFaceSpillCounterIndex(capacity) + 1u + tileIndex;
}

uint sourceFaceSpillNodeIndex(uint capacity, uint spill) {
    return sourceFaceSpillCounterIndex(capacity) + 1u + kSourceFaceTileCount + spill * kSourceFaceSpillNodeWords;
}

uint sourceFaceHierarchyNeededIndex(uint capacity) {
    return sourceFaceSpillNodeIndex(capacity, kSourceFaceSpillCapacity);
}

uint sourceFaceHierarchyReadyIndex(uint capacity) {
    return sourceFaceHierarchyNeededIndex(capacity) + 1u;
}

uint sourceFaceHierarchyNodeIndex(uint capacity, uint node) {
    return sourceFaceHierarchyReadyIndex(capacity) + 1u + node * kSourceFaceHierarchyNodeWords;
}

// The order region follows the 2 * capacity / kSourceFaceHierarchyLeafSize hierarchy nodes.
uint sourceFaceHierarchyOrderIndex(uint capacity) {
    return sourceFaceHierarchyNodeIndex(capacity, 2u * (capacity / kSourceFaceHierarchyLeafSize));
}

uint sourceFaceHierarchyOrderEntriesIndex(uint capacity) {
    return sourceFaceHierarchyOrderIndex(capacity) + kSourceFaceHierarchyOrderBuckets;
}

// Slab of a packed primary word; depth-major, so the splat byte never moves it.
uint sunDepthSlab(uint packedDepth) {
    return (packedDepth >> 8) / kSunDepthSlabQuantized;
}

// Planar storage: every slot's min words form one texel-indexed plane, then
// its max words, so a clear or a splat box touches consecutive words.
uint sunDepthSlabMinIndex(int texelIndex, uint slot) {
    return kSunDepthSlabOffset + slot * kSunDepthSlabWords * kSourceFaceFallbackOffset + uint(texelIndex);
}

uint sunDepthSlabMaxIndex(int texelIndex, uint slot) {
    return sunDepthSlabMinIndex(texelIndex, slot) + kSourceFaceFallbackOffset;
}

uint sourceFaceTileBase(uint tileIndex) {
    return kSourceFaceTileOffset + tileIndex * (kSourceFaceTileCapacity + 1u);
}

bool sourceFaceQueryComplete(uint count) {
    return count <= kSourceFaceTileQueryCapacity;
}

bool sourceFaceTileRetainsRecords(uint count) {
    return (count & kSourceFaceTileRecordLost) == 0u;
}

// Reservations past capacity publish nothing; every lower ID holds a published record.
uint sourceFaceRetainedRecords(uint requested, uint capacity) {
    return requested < capacity ? requested : capacity;
}

// Positive separation is a front hit, including conservative rounded boundaries.
float sourceFaceRaySeparation(vec2 sunUV, float sunZ, vec3 corner, vec3 edgeU, vec3 edgeV
#if IR_SUN_QUERY_STATS
    , inout SunQueryStats sunQueryStats
#endif
) {
    const float determinant = projectedFaceDeterminant(vec2(edgeU.x, edgeU.y), vec2(edgeV.x, edgeV.y));
    if (abs(determinant) < 0.000001 ||
        !sourceFaceWithinProjectedBounds(sunUV.x, corner.x, edgeU.x, edgeV.x) ||
        !sourceFaceWithinProjectedBounds(sunUV.y, corner.y, edgeU.y, edgeV.y)) {
#if IR_SUN_QUERY_STATS
        sunQueryStats.counts[kSunQueryFaceMisses] += 1u;
#endif
        return -1.0;
    }
    const vec2 uv = projectedFaceCoordinates(
        vec2(sunUV.x - corner.x, sunUV.y - corner.y),
        vec2(edgeU.x, edgeU.y), vec2(edgeV.x, edgeV.y), determinant);
    if (!projectedFaceContainsClosed(sunUV, vec2(corner.x, corner.y),
            vec2(edgeU.x, edgeU.y), vec2(edgeV.x, edgeV.y), determinant, uv)) {
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
