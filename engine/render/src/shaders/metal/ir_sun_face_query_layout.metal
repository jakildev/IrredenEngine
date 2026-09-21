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
    const float determinant = edgeU.x * edgeV.y - edgeU.y * edgeV.x;
    if (abs(determinant) < 0.000001) return -1.0;
    const float deltaU = sunUV.x - corner.x;
    const float deltaV = sunUV.y - corner.y;
    const float u = (deltaU * edgeV.y - deltaV * edgeV.x) / determinant;
    const float v = (edgeU.x * deltaV - edgeU.y * deltaU) / determinant;
    if (u < 0.0 || v < 0.0 || u > 1.0 || v > 1.0) return -1.0;
    return sunZ - (corner.z + u * edgeU.z + v * edgeV.z);
}

#endif
