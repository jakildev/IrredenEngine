#include "ir_projected_face.glsl"
#ifndef IR_SUN_FACE_QUERY_LAYOUT_INCLUDED
#define IR_SUN_FACE_QUERY_LAYOUT_INCLUDED

// SunShadowDepthMap holds depth layers, primary tile lists, geometry and bounded spill lists.
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
const uint kSourceFaceHeaderOffset = 2u * kSourceFaceFallbackOffset;
const uint kSourceFaceTileOffset = kSourceFaceHeaderOffset + 1u;
const uint kSourceFaceRecordOffset = kSourceFaceTileOffset + kSourceFaceTileCount * (kSourceFaceTileCapacity + 1u);
const uint kSourceFaceSpillCounterOffset = kSourceFaceRecordOffset + kSourceFaceCapacity * kSourceFaceRecordWords;
const uint kSourceFaceSpillHeadOffset = kSourceFaceSpillCounterOffset + 1u;
const uint kSourceFaceSpillNodeOffset = kSourceFaceSpillHeadOffset + kSourceFaceTileCount;
const uint kSourceFaceHierarchyNeededOffset = kSourceFaceSpillNodeOffset + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords;
const uint kSourceFaceHierarchyReadyOffset = kSourceFaceHierarchyNeededOffset + 1u;
const uint kSourceFaceHierarchyOffset = kSourceFaceHierarchyReadyOffset + 1u;
const uint kSourceFaceHierarchyLeafSize = 8u;
const uint kSourceFaceHierarchyMaxLeaves = kSourceFaceCapacity / kSourceFaceHierarchyLeafSize;
const uint kSourceFaceHierarchyNodeWords = 8u;
const uint kSourceFaceHierarchyWorkgroupSize = 128u;
const uint kSourceFaceHierarchyBuildPasses = 2u;
const uint kSourceFaceBufferWords = kSourceFaceHierarchyOffset + 2u * kSourceFaceHierarchyMaxLeaves * kSourceFaceHierarchyNodeWords;

// Every header access passes `uint(sunDepthBuf.length())`: NVIDIA's cold link
// time grows with a compile-time-constant SSBO index, and this one costs minutes
// per program, while a runtime-derived index links in well under a second.
// Addresses the header word only while binding 28 holds the whole
// SunShadowDepthMap; a bindRange, a resize not derived from
// kSourceFaceBufferWords, or another buffer aliased onto slot 28 misaddresses it.
uint sourceFaceHeaderIndex(uint boundWords) {
    return boundWords - (kSourceFaceBufferWords - kSourceFaceHeaderOffset);
}

uint sourceFaceSpillCounterIndex(uint boundWords) {
    return boundWords - (kSourceFaceBufferWords - kSourceFaceSpillCounterOffset);
}

uint sourceFaceHierarchyNeededIndex(uint boundWords) {
    return boundWords - (kSourceFaceBufferWords - kSourceFaceHierarchyNeededOffset);
}

uint sourceFaceHierarchyReadyIndex(uint boundWords) {
    return boundWords - (kSourceFaceBufferWords - kSourceFaceHierarchyReadyOffset);
}

uint sourceFaceTileBase(uint tileIndex) {
    return kSourceFaceTileOffset + tileIndex * (kSourceFaceTileCapacity + 1u);
}

bool sourceFaceQueryComplete(uint count) {
    return count <= kSourceFaceTileQueryCapacity;
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
