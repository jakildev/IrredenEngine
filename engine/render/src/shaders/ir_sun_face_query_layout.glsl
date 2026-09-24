#include "ir_projected_face.glsl"
#ifndef IR_SUN_FACE_QUERY_LAYOUT_INCLUDED
#define IR_SUN_FACE_QUERY_LAYOUT_INCLUDED

// Tail of SunShadowDepthMap: face count, bounded tile lists, then projected face records.
const uint kSourceFaceMapDimension = 1024u;
const uint kSourceFaceCascadeCount = 2u;
const uint kSourceFaceTileEdge = 8u;
const uint kSourceFaceTilesPerAxis = kSourceFaceMapDimension / kSourceFaceTileEdge;
const uint kSourceFaceTileCount = kSourceFaceCascadeCount * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis;
const uint kSourceFaceTileCapacity = 64u;
const uint kSourceFaceCapacity = 65536u;
const uint kSourceFaceRecordWords = 9u;
const uint kSourceFaceFallbackOffset = kSourceFaceCascadeCount * kSourceFaceMapDimension * kSourceFaceMapDimension;
const uint kSourceFaceHeaderOffset = 2u * kSourceFaceFallbackOffset;
const uint kSourceFaceTileOffset = kSourceFaceHeaderOffset + 1u;
const uint kSourceFaceRecordOffset = kSourceFaceTileOffset + kSourceFaceTileCount * (kSourceFaceTileCapacity + 1u);
const uint kSourceFaceBufferWords = kSourceFaceRecordOffset + kSourceFaceCapacity * kSourceFaceRecordWords;

// Every header access passes `uint(sunDepthBuf.length())`: NVIDIA's cold link
// time grows with a compile-time-constant SSBO index, and this one costs minutes
// per program, while a runtime-derived index links in well under a second.
// Addresses the header word only while binding 28 holds the whole
// SunShadowDepthMap; a bindRange, a resize not derived from
// kSourceFaceBufferWords, or another buffer aliased onto slot 28 misaddresses it.
uint sourceFaceHeaderIndex(uint boundWords) {
    return boundWords - (kSourceFaceBufferWords - kSourceFaceHeaderOffset);
}

uint sourceFaceTileBase(uint tileIndex) {
    return kSourceFaceTileOffset + tileIndex * (kSourceFaceTileCapacity + 1u);
}

bool sourceFaceQueryComplete(uint count) {
    return count <= kSourceFaceTileCapacity;
}

// Positive separation means the receiver ray intersects this finite face in front.
float sourceFaceRaySeparation(vec2 sunUV, float sunZ, vec3 corner, vec3 edgeU, vec3 edgeV) {
    const float determinant = projectedFaceDeterminant(vec2(edgeU.x, edgeU.y), vec2(edgeV.x, edgeV.y));
    if (abs(determinant) < 0.000001) return -1.0;
    const vec2 uv = projectedFaceCoordinates(
        vec2(sunUV.x - corner.x, sunUV.y - corner.y),
        vec2(edgeU.x, edgeU.y), vec2(edgeV.x, edgeV.y), determinant);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return -1.0;
    return sunZ - (corner.z + uv.x * edgeU.z + uv.y * edgeV.z);
}

#endif
