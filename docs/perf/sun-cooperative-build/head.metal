#define IR_SUN_QUERY_STATS 0
#include <metal_stdlib>
using namespace metal;
#ifndef IR_PROJECTED_FACE_INCLUDED
#define IR_PROJECTED_FACE_INCLUDED

inline float projectedFaceDeterminant(float2 edgeU, float2 edgeV) {
    return edgeU.x * edgeV.y - edgeU.y * edgeV.x;
}

// The caller rejects degenerate bases before dividing. Coverage ownership is
// separate: display faces are half-open; finite shadow footprints are closed.
inline float2 projectedFaceCoordinates(float2 delta, float2 edgeU, float2 edgeV, float determinant) {
    return float2((delta.x * edgeV.y - delta.y * edgeV.x) / determinant,
                (edgeU.x * delta.y - edgeU.y * delta.x) / determinant);
}

// The sun-query envelope preserves exact represented faces at every orientation.
// Its arithmetic margin and the producer enclosure in sourceFaceIndexBounds
// are coupled; the derivation is in docs/design/sun-face-index-coverage.md.
inline bool sourceFaceWithinProjectedBounds(float point, float corner, float edgeU, float edgeV) {
    const float relativeMargin = 0.00000095367431640625;
    const float absoluteMargin = 3.7615819226313200255e-37;
    const float padding = relativeMargin * (abs(corner) + abs(edgeU) + abs(edgeV)) + absoluteMargin;
    const float low = corner + (edgeU < 0.0 ? edgeU : 0.0) + (edgeV < 0.0 ? edgeV : 0.0) - padding;
    const float high = corner + (edgeU > 0.0 ? edgeU : 0.0) + (edgeV > 0.0 ? edgeV : 0.0) + padding;
    return !(point < low || point > high);
}

// Conservative closed-edge predicates for independently rounded projected
// inputs and finite normal binary32 arithmetic. gamma(7) bounds input rounding,
// subtraction and two-product edge functions, not upstream projection error.
// The caller rejects degenerate bases and keeps uncertain-hit depth on the face.
inline bool projectedFaceContainsClosed(float2 point, float2 corner, float2 edgeU, float2 edgeV,
                                       float determinant, float2 uv) {
    if (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0) return true;
    const float unitRoundoff = 0.000000059604644775390625;
    const float gamma7 = (7.0 * unitRoundoff) / (1.0 - 7.0 * unitRoundoff);
    const float dx = abs(point.x) + abs(corner.x);
    const float dy = abs(point.y) + abs(corner.y);
    const float detMagnitude = abs(edgeU.x * edgeV.y) + abs(edgeU.y * edgeV.x);
    const float errorU = gamma7 * (dx * abs(edgeV.y) + dy * abs(edgeV.x) + detMagnitude);
    const float errorV = gamma7 * (dx * abs(edgeU.y) + dy * abs(edgeU.x) + detMagnitude);
    const float deltaX = point.x - corner.x;
    const float deltaY = point.y - corner.y;
    const float numeratorU = deltaX * edgeV.y - deltaY * edgeV.x;
    const float numeratorV = edgeU.x * deltaY - edgeU.y * deltaX;
    const float orientation = determinant > 0.0 ? 1.0 : -1.0;
    return orientation * numeratorU >= -errorU &&
           orientation * (determinant - numeratorU) >= -errorU &&
           orientation * numeratorV >= -errorV &&
           orientation * (determinant - numeratorV) >= -errorV;
}

#endif

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
constant uint kSourceFaceHierarchyWorkgroupSize = 128u;
constant uint kSourceFaceHierarchyBuildPasses = 2u;
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

struct SunHierarchyFrame { int4 hierarchyDispatch; };
#ifndef IR_SUN_GEOMETRY_STORAGE_INCLUDED
#define IR_SUN_GEOMETRY_STORAGE_INCLUDED
#ifndef IR_SUN_HIERARCHY_BOUNDS_INCLUDED
#define IR_SUN_HIERARCHY_BOUNDS_INCLUDED
#ifndef IR_SUN_FACE_BOUNDS_INCLUDED
#define IR_SUN_FACE_BOUNDS_INCLUDED

inline bool sourceFaceBoundFinite(float value) {
    return (as_type<uint>(value) & 0x7F800000u) != 0x7F800000u;
}

inline bool sourceFaceBoundNormalOrZero(float value) {
    const uint magnitude = as_type<uint>(value) & 0x7FFFFFFFu;
    return magnitude == 0u || (magnitude >= 0x00800000u && magnitude < 0x7F800000u);
}

// The minimum-normal enclosure also covers arithmetic that flushes subnormals.
inline float sourceFaceBoundUp(float value) {
    const uint bits = as_type<uint>(value);
    const uint magnitude = bits & 0x7FFFFFFFu;
    if (magnitude >= 0x7F800000u) return value;
    if (magnitude <= 0x00800000u) return as_type<float>(0x00800001u);
    return as_type<float>((bits & 0x80000000u) != 0u ? bits - 1u : bits + 1u);
}

inline float sourceFaceBoundDown(float value) {
    const uint bits = as_type<uint>(value);
    const uint magnitude = bits & 0x7FFFFFFFu;
    if (magnitude >= 0x7F800000u) return value;
    if (magnitude <= 0x00800000u) return as_type<float>(0x80800001u);
    return as_type<float>((bits & 0x80000000u) != 0u ? bits + 1u : bits - 1u);
}

// Independently compiled producer and receiver determinants may contract differently.
inline bool sourceFaceDefinitelyDegenerate(float2 edgeU, float2 edgeV, float determinant) {
    if (!sourceFaceBoundNormalOrZero(edgeU.x) || !sourceFaceBoundNormalOrZero(edgeU.y) ||
        !sourceFaceBoundNormalOrZero(edgeV.x) || !sourceFaceBoundNormalOrZero(edgeV.y) ||
        !sourceFaceBoundNormalOrZero(determinant)) return false;
    const float2 absoluteU = abs(edgeU);
    const float2 absoluteV = abs(edgeV);
    const float sumU = sourceFaceBoundUp(absoluteU.x + absoluteU.y);
    const float sumV = sourceFaceBoundUp(absoluteV.x + absoluteV.y);
    const float determinantMagnitude = sourceFaceBoundUp(
        sourceFaceBoundUp(absoluteU.x * absoluteV.y) +
        sourceFaceBoundUp(absoluteU.y * absoluteV.x));
    const float relativeError = as_type<float>(0x35800009u);
    const float absoluteError = sourceFaceBoundUp(
        sourceFaceBoundUp(32.0 * as_type<float>(0x00800000u)) *
        sourceFaceBoundUp(1.0 + sourceFaceBoundUp(sumU + sumV)));
    const float error = sourceFaceBoundUp(
        sourceFaceBoundUp(relativeError * determinantMagnitude) + absoluteError);
    const float receiverMagnitude = sourceFaceBoundUp(abs(determinant) + error);
    return sourceFaceBoundFinite(receiverMagnitude) && receiverMagnitude < 0.000001;
}

inline bool sourceFaceCascadeArithmeticSafe(float2 origin, float2 cellSize) {
    const uint originX = as_type<uint>(origin.x) & 0x7FFFFFFFu;
    const uint originY = as_type<uint>(origin.y) & 0x7FFFFFFFu;
    const uint sizeX = as_type<uint>(cellSize.x);
    const uint sizeY = as_type<uint>(cellSize.y);
    return (originX == 0u || originX >= 0x00800000u) &&
           (originY == 0u || originY >= 0x00800000u) &&
           originX <= 0x5C800000u && originY <= 0x5C800000u &&
           sizeX >= 0x00800000u && sizeX <= 0x58800000u &&
           sizeY >= 0x00800000u && sizeY <= 0x58800000u;
}

// The interval encloses receiver coordinates before floor and integer conversion.
inline float2 sourceFaceTileInterval(float low, float high, float origin, float cellSize) {
    const float minimumNormal = as_type<float>(0x00800000u);
    const float maximumFinite = as_type<float>(0x7F7FFFFFu);
    const float differenceLow = sourceFaceBoundDown(low - origin);
    const float differenceHigh = sourceFaceBoundUp(high - origin);
    if (!sourceFaceBoundFinite(differenceLow) || !sourceFaceBoundFinite(differenceHigh))
        return float2(-maximumFinite, maximumFinite);
    const float differenceMagnitude = max(abs(differenceLow), abs(differenceHigh));
    const float subtractionError = sourceFaceBoundUp(
        sourceFaceBoundUp(0.000000059604644775390625 * differenceMagnitude) +
        3.0 * minimumNormal);
    const float numeratorLow = sourceFaceBoundDown(differenceLow - subtractionError);
    const float numeratorHigh = sourceFaceBoundUp(differenceHigh + subtractionError);
    const float quotientLow = numeratorLow / cellSize;
    const float quotientHigh = numeratorHigh / cellSize;
    if (!sourceFaceBoundFinite(quotientLow) || !sourceFaceBoundFinite(quotientHigh))
        return float2(-maximumFinite, maximumFinite);
    const float quotientMagnitude = max(abs(quotientLow), abs(quotientHigh));
    const float oneDivisionError = sourceFaceBoundUp(
        sourceFaceBoundUp(0.000000476837158203125 * quotientMagnitude) + minimumNormal);
    const float divisionError = sourceFaceBoundUp(2.0 * sourceFaceBoundUp(
        oneDivisionError * 1.00000059604644775390625));
    const float2 interval = float2(sourceFaceBoundDown(quotientLow - divisionError),
                               sourceFaceBoundUp(quotientHigh + divisionError));
    if (!sourceFaceBoundFinite(interval.x) || !sourceFaceBoundFinite(interval.y))
        return float2(-maximumFinite, maximumFinite);
    return interval;
}

// False requests full-cascade indexing. The arithmetic contract and padding
// derivation are in docs/design/sun-face-index-coverage.md.
inline bool sourceFaceIndexBounds(float2 corner, float2 edgeU, float2 edgeV, float determinant,
                           thread float2 &low, thread float2 &high) {
    if (!sourceFaceBoundNormalOrZero(corner.x) || !sourceFaceBoundNormalOrZero(corner.y) ||
        !sourceFaceBoundNormalOrZero(edgeU.x) || !sourceFaceBoundNormalOrZero(edgeU.y) ||
        !sourceFaceBoundNormalOrZero(edgeV.x) || !sourceFaceBoundNormalOrZero(edgeV.y) ||
        !sourceFaceBoundNormalOrZero(determinant)) return false;

    const float2 absoluteCorner = abs(corner);
    const float2 absoluteU = abs(edgeU);
    const float2 absoluteV = abs(edgeV);
    const float maximumInput = as_type<float>(0x5D800000u);
    if (max(absoluteCorner.x, absoluteCorner.y) > maximumInput ||
        max(absoluteU.x, absoluteU.y) > maximumInput ||
        max(absoluteV.x, absoluteV.y) > maximumInput) return false;
    const float2 extent = float2(
        sourceFaceBoundUp(sourceFaceBoundUp(absoluteCorner.x + absoluteU.x) + absoluteV.x),
        sourceFaceBoundUp(sourceFaceBoundUp(absoluteCorner.y + absoluteU.y) + absoluteV.y));
    // The receiver margin is smaller: this encloses independently rounded
    // sourceFaceWithinProjectedBounds endpoints, including contraction and FTZ.
    const float relativeMargin = 0.0000019073486328125;
    const float absoluteMargin = 64.0 * as_type<float>(0x00800000u);
    const float2 padding = float2(
        sourceFaceBoundUp(sourceFaceBoundUp(relativeMargin * extent.x) + absoluteMargin),
        sourceFaceBoundUp(sourceFaceBoundUp(relativeMargin * extent.y) + absoluteMargin));

    low = float2(
        sourceFaceBoundDown(sourceFaceBoundDown(
            sourceFaceBoundDown(corner.x + min(0.0, edgeU.x)) + min(0.0, edgeV.x)) - padding.x),
        sourceFaceBoundDown(sourceFaceBoundDown(
            sourceFaceBoundDown(corner.y + min(0.0, edgeU.y)) + min(0.0, edgeV.y)) - padding.y));
    high = float2(
        sourceFaceBoundUp(sourceFaceBoundUp(
            sourceFaceBoundUp(corner.x + max(0.0, edgeU.x)) + max(0.0, edgeV.x)) + padding.x),
        sourceFaceBoundUp(sourceFaceBoundUp(
            sourceFaceBoundUp(corner.y + max(0.0, edgeU.y)) + max(0.0, edgeV.y)) + padding.y));
    return sourceFaceBoundFinite(low.x) && sourceFaceBoundFinite(low.y) &&
           sourceFaceBoundFinite(high.x) && sourceFaceBoundFinite(high.y);
}

#endif

#ifndef IR_PROJECTED_FACE_INCLUDED
#define IR_PROJECTED_FACE_INCLUDED

inline float projectedFaceDeterminant(float2 edgeU, float2 edgeV) {
    return edgeU.x * edgeV.y - edgeU.y * edgeV.x;
}

// The caller rejects degenerate bases before dividing. Coverage ownership is
// separate: display faces are half-open; finite shadow footprints are closed.
inline float2 projectedFaceCoordinates(float2 delta, float2 edgeU, float2 edgeV, float determinant) {
    return float2((delta.x * edgeV.y - delta.y * edgeV.x) / determinant,
                (edgeU.x * delta.y - edgeU.y * delta.x) / determinant);
}

// The sun-query envelope preserves exact represented faces at every orientation.
// Its arithmetic margin and the producer enclosure in sourceFaceIndexBounds
// are coupled; the derivation is in docs/design/sun-face-index-coverage.md.
inline bool sourceFaceWithinProjectedBounds(float point, float corner, float edgeU, float edgeV) {
    const float relativeMargin = 0.00000095367431640625;
    const float absoluteMargin = 3.7615819226313200255e-37;
    const float padding = relativeMargin * (abs(corner) + abs(edgeU) + abs(edgeV)) + absoluteMargin;
    const float low = corner + (edgeU < 0.0 ? edgeU : 0.0) + (edgeV < 0.0 ? edgeV : 0.0) - padding;
    const float high = corner + (edgeU > 0.0 ? edgeU : 0.0) + (edgeV > 0.0 ? edgeV : 0.0) + padding;
    return !(point < low || point > high);
}

// Conservative closed-edge predicates for independently rounded projected
// inputs and finite normal binary32 arithmetic. gamma(7) bounds input rounding,
// subtraction and two-product edge functions, not upstream projection error.
// The caller rejects degenerate bases and keeps uncertain-hit depth on the face.
inline bool projectedFaceContainsClosed(float2 point, float2 corner, float2 edgeU, float2 edgeV,
                                       float determinant, float2 uv) {
    if (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0) return true;
    const float unitRoundoff = 0.000000059604644775390625;
    const float gamma7 = (7.0 * unitRoundoff) / (1.0 - 7.0 * unitRoundoff);
    const float dx = abs(point.x) + abs(corner.x);
    const float dy = abs(point.y) + abs(corner.y);
    const float detMagnitude = abs(edgeU.x * edgeV.y) + abs(edgeU.y * edgeV.x);
    const float errorU = gamma7 * (dx * abs(edgeV.y) + dy * abs(edgeV.x) + detMagnitude);
    const float errorV = gamma7 * (dx * abs(edgeU.y) + dy * abs(edgeU.x) + detMagnitude);
    const float deltaX = point.x - corner.x;
    const float deltaY = point.y - corner.y;
    const float numeratorU = deltaX * edgeV.y - deltaY * edgeV.x;
    const float numeratorV = edgeU.x * deltaY - edgeU.y * deltaX;
    const float orientation = determinant > 0.0 ? 1.0 : -1.0;
    return orientation * numeratorU >= -errorU &&
           orientation * (determinant - numeratorU) >= -errorU &&
           orientation * numeratorV >= -errorV &&
           orientation * (determinant - numeratorV) >= -errorV;
}

#endif

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
constant uint kSourceFaceHierarchyWorkgroupSize = 128u;
constant uint kSourceFaceHierarchyBuildPasses = 2u;
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

struct SourceSunGeometry { float3 corner; float3 edgeU; float3 edgeV; uint type; };
// low.w is -1 for empty, 0 for bounded, 1 for an uncertifiable enclosure.
struct SourceSunBounds { float4 low; float4 high; };

inline SourceSunBounds sourceSunEmptyBounds() {
    return SourceSunBounds{float4(0.0, 0.0, 0.0, -1.0), float4(0.0)};
}

inline SourceSunBounds sourceSunMergeBounds(SourceSunBounds a, SourceSunBounds b) {
    if (a.low.w < 0.0) return b;
    if (b.low.w < 0.0) return a;
    return SourceSunBounds{float4(min(a.low.xyz, b.low.xyz), max(a.low.w, b.low.w)),
                           float4(max(a.high.xyz, b.high.xyz), 0.0)};
}

inline SourceSunBounds sourceSunGeometryBounds(SourceSunGeometry geometry) {
    float2 lowUV, highUV, lowZ, highZ;
    const float determinant = projectedFaceDeterminant(geometry.edgeU.xy, geometry.edgeV.xy);
    // Analytic records retain their exact predicate; no finite-quad bound applies.
    const bool bounded = geometry.type == 0u &&
        sourceFaceIndexBounds(geometry.corner.xy, geometry.edgeU.xy, geometry.edgeV.xy,
                              determinant, lowUV, highUV) &&
        sourceFaceIndexBounds(float2(geometry.corner.z, 0.0), float2(geometry.edgeU.z, 0.0),
                              float2(geometry.edgeV.z, 0.0), 0.0, lowZ, highZ);
    if (!bounded) return SourceSunBounds{float4(0.0, 0.0, 0.0, 1.0), float4(0.0)};
    return SourceSunBounds{float4(lowUV, lowZ.x, 0.0), float4(highUV, highZ.x, 0.0)};
}

inline bool sourceSunBoundsOverlap(SourceSunBounds bounds, float2 sunUV, float sunZ,
                            float bias, float maxShadowThrow) {
    if (bounds.low.w < 0.0) return false;
    if (bounds.low.w > 0.0) return true;
    return all(sunUV >= bounds.low.xy) && all(sunUV <= bounds.high.xy) &&
        sourceFaceBoundUp(sunZ - bounds.low.z) > bias &&
        sourceFaceBoundDown(sunZ - bounds.high.z) < maxShadowThrow;
}

inline uint sourceSunHierarchyLeafCount(uint records) {
    uint leaves = 1u;
    while (leaves * kSourceFaceHierarchyLeafSize < records) leaves *= 2u;
    return leaves;
}
#endif

inline SourceSunGeometry readSourceSunGeometry(device const uint* sunDepthBuf, uint id) {
    const uint record = kSourceFaceRecordOffset + id * kSourceFaceRecordWords;
    return SourceSunGeometry{
        float3(as_type<float>(sunDepthBuf[record]), as_type<float>(sunDepthBuf[record + 1u]), as_type<float>(sunDepthBuf[record + 2u])),
        float3(as_type<float>(sunDepthBuf[record + 3u]), as_type<float>(sunDepthBuf[record + 4u]), as_type<float>(sunDepthBuf[record + 5u])),
        float3(as_type<float>(sunDepthBuf[record + 6u]), as_type<float>(sunDepthBuf[record + 7u]), as_type<float>(sunDepthBuf[record + 8u])),
        sunDepthBuf[record + 9u]};
}

// Word loads avoid imposing vector alignment on the appended SSBO range.
inline SourceSunBounds readSourceSunBounds(device const uint* sunDepthBuf, uint node) {
    const uint offset = kSourceFaceHierarchyOffset + node * kSourceFaceHierarchyNodeWords;
    SourceSunBounds bounds;
    for (uint axis = 0u; axis < 4u; ++axis) {
        bounds.low[axis] = as_type<float>(sunDepthBuf[offset + axis]);
        bounds.high[axis] = as_type<float>(sunDepthBuf[offset + 4u + axis]);
    }
    return bounds;
}
#endif

inline void writeSourceSunBounds(device uint* sunDepthBuf, uint node, SourceSunBounds bounds) {
    const uint offset = kSourceFaceHierarchyOffset + node * kSourceFaceHierarchyNodeWords;
    for (uint axis = 0u; axis < 4u; ++axis) {
        sunDepthBuf[offset + axis] = as_type<uint>(bounds.low[axis]);
        sunDepthBuf[offset + 4u + axis] = as_type<uint>(bounds.high[axis]);
    }
}

inline SourceSunBounds sourceSunLeafBounds(device const uint* sunDepthBuf, uint leaf, uint records) {
    SourceSunBounds bounds = sourceSunEmptyBounds();
    const uint first = leaf * kSourceFaceHierarchyLeafSize;
    for (uint i = first; i < min(records, first + kSourceFaceHierarchyLeafSize); ++i)
        bounds = sourceSunMergeBounds(bounds, sourceSunGeometryBounds(readSourceSunGeometry(sunDepthBuf, i)));
    return bounds;
}

inline void initializeSourceSunHierarchy(device uint* sunDepthBuf,
    threadgroup SourceSunBounds* hierarchyBounds, uint lane, uint pass, uint rootNode,
    uint width, uint leaves, uint records) {
    if (lane >= width) return;
    const uint node = rootNode * width + lane;
    SourceSunBounds bounds;
    if (pass == 0u) {
        bounds = sourceSunLeafBounds(sunDepthBuf, node - leaves, records);
        writeSourceSunBounds(sunDepthBuf, node, bounds);
    } else {
        bounds = readSourceSunBounds(sunDepthBuf, node);
    }
    hierarchyBounds[width + lane] = bounds;
}

inline void reduceSourceSunHierarchy(device uint* sunDepthBuf,
    threadgroup SourceSunBounds* hierarchyBounds, uint lane, uint level, uint rootNode) {
    if (lane >= level) return;
    const uint node = level + lane;
    const SourceSunBounds bounds = sourceSunMergeBounds(
        hierarchyBounds[node * 2u], hierarchyBounds[node * 2u + 1u]);
    hierarchyBounds[node] = bounds;
    writeSourceSunBounds(sunDepthBuf, rootNode * level + lane, bounds);
}

kernel void c_build_sun_hierarchy(device uint* sunDepthBuf [[buffer(28)]],
    constant SunHierarchyFrame& params [[buffer(16)]],
    uint lane [[thread_index_in_threadgroup]], uint3 groupId [[threadgroup_position_in_grid]]) {
    threadgroup SourceSunBounds hierarchyBounds[2u * kSourceFaceHierarchyWorkgroupSize];
    const uint group = groupId.x;
    const uint records = sunDepthBuf[kSourceFaceHeaderOffset];
    if (records == 0u || records > kSourceFaceCapacity ||
        sunDepthBuf[kSourceFaceHierarchyNeededOffset] == 0u) return;
    const uint leaves = sourceSunHierarchyLeafCount(records);
    const uint pass = uint(params.hierarchyDispatch.x);
    const uint subtreeLeaves = min(leaves, kSourceFaceHierarchyWorkgroupSize);
    const uint subtrees = leaves / subtreeLeaves;
    // Every kernel return is group-uniform; inactive lanes still reach each barrier.
    if (pass >= kSourceFaceHierarchyBuildPasses ||
        (pass == 0u ? group >= subtrees : group != 0u)) return;
    const uint width = pass == 0u ? subtreeLeaves : subtrees;
    const uint rootNode = pass == 0u ? subtrees + group : 1u;
    // At local width W, lane i owns global node rootNode * W + i.
    initializeSourceSunHierarchy(sunDepthBuf, hierarchyBounds, lane, pass, rootNode, width, leaves, records);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint level = width / 2u; level > 0u; level /= 2u) {
        reduceSourceSunHierarchy(sunDepthBuf, hierarchyBounds, lane, level, rootNode);
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    // Both passes and consumers are separated by dispatch storage barriers.
    if (pass == 1u && lane == 0u)
        sunDepthBuf[kSourceFaceHierarchyReadyOffset] = leaves;
}
