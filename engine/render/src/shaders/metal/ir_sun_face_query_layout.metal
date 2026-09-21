#include "ir_projected_face.metal"
#ifndef IR_SUN_FACE_QUERY_LAYOUT_INCLUDED
#define IR_SUN_FACE_QUERY_LAYOUT_INCLUDED

// Tail of SunShadowDepthMap: face count, bounded tile lists, then projected face records.
constant uint kSourceFaceMapDimension = 1024u;
constant uint kSourceFaceCascadeCount = 2u;
constant uint kSourceFaceTileEdge = 8u;
constant uint kSourceFaceTilesPerAxis = kSourceFaceMapDimension / kSourceFaceTileEdge;
constant uint kSourceFaceTileCount = kSourceFaceCascadeCount * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis;
constant uint kSourceFaceTileCapacity = 64u;
constant uint kSourceFaceCapacity = 65536u;
constant uint kSourceFaceRecordWords = 9u;
constant uint kSourceFaceFallbackOffset = kSourceFaceCascadeCount * kSourceFaceMapDimension * kSourceFaceMapDimension;
constant uint kSourceFaceHeaderOffset = 2u * kSourceFaceFallbackOffset;
constant uint kSourceFaceTileOffset = kSourceFaceHeaderOffset + 1u;
constant uint kSourceFaceRecordOffset = kSourceFaceTileOffset + kSourceFaceTileCount * (kSourceFaceTileCapacity + 1u);
constant uint kSourceFaceBufferWords = kSourceFaceRecordOffset + kSourceFaceCapacity * kSourceFaceRecordWords;

inline uint sourceFaceTileBase(uint tileIndex) {
    return kSourceFaceTileOffset + tileIndex * (kSourceFaceTileCapacity + 1u);
}

inline bool sourceFaceQueryComplete(uint count) {
    return count <= kSourceFaceTileCapacity;
}

// The marker distinguishes query-backed faces from analytic surface samples.
inline bool sunWriteIsSourceFace(uint packedDepth) {
    return (packedDepth & 0xFFu) == 0x89u;
}

// Positive separation means the receiver ray intersects this finite face in front.
inline float sourceFaceRaySeparation(float2 sunUV, float sunZ, float3 corner, float3 edgeU, float3 edgeV) {
    const float determinant = projectedFaceDeterminant(float2(edgeU.x, edgeU.y), float2(edgeV.x, edgeV.y));
    if (abs(determinant) < 0.000001) return -1.0;
    const float2 uv = projectedFaceCoordinates(
        float2(sunUV.x - corner.x, sunUV.y - corner.y),
        float2(edgeU.x, edgeU.y), float2(edgeV.x, edgeV.y), determinant);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return -1.0;
    return sunZ - (corner.z + uv.x * edgeU.z + uv.y * edgeV.z);
}

#endif
