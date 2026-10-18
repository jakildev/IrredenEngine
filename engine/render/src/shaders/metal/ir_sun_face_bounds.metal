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
