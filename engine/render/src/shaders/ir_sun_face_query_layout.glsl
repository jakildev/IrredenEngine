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

uint sourceFaceTileBase(uint tileIndex) {
    return kSourceFaceTileOffset + tileIndex * (kSourceFaceTileCapacity + 1u);
}

bool sourceFaceQueryComplete(uint count) {
    return count <= kSourceFaceTileCapacity;
}

// The marker distinguishes query-backed faces from analytic surface samples.
bool sunWriteIsSourceFace(uint packedDepth) {
    return (packedDepth & 0xFFu) == 0x89u;
}

// Positive separation means the receiver ray intersects this finite face in front.
float sourceFaceRaySeparation(vec2 sunUV, float sunZ, vec3 corner, vec3 edgeU, vec3 edgeV) {
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
