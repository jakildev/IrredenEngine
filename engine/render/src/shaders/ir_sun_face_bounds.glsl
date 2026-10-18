#ifndef IR_SUN_FACE_BOUNDS_INCLUDED
#define IR_SUN_FACE_BOUNDS_INCLUDED

bool sourceFaceBoundFinite(float value) {
    return (floatBitsToUint(value) & 0x7F800000u) != 0x7F800000u;
}

bool sourceFaceBoundNormalOrZero(float value) {
    const uint magnitude = floatBitsToUint(value) & 0x7FFFFFFFu;
    return magnitude == 0u || (magnitude >= 0x00800000u && magnitude < 0x7F800000u);
}

// The minimum-normal enclosure also covers arithmetic that flushes subnormals.
float sourceFaceBoundUp(float value) {
    const uint bits = floatBitsToUint(value);
    const uint magnitude = bits & 0x7FFFFFFFu;
    if (magnitude >= 0x7F800000u) return value;
    if (magnitude <= 0x00800000u) return uintBitsToFloat(0x00800001u);
    return uintBitsToFloat((bits & 0x80000000u) != 0u ? bits - 1u : bits + 1u);
}

float sourceFaceBoundDown(float value) {
    const uint bits = floatBitsToUint(value);
    const uint magnitude = bits & 0x7FFFFFFFu;
    if (magnitude >= 0x7F800000u) return value;
    if (magnitude <= 0x00800000u) return uintBitsToFloat(0x80800001u);
    return uintBitsToFloat((bits & 0x80000000u) != 0u ? bits + 1u : bits - 1u);
}

// Four quotient ULPs give relative error at most 8u. This binary32 factor
// exceeds 1/(1-8u); one outward step encloses the final multiplication.
float sourceFaceBoundDivideUp(float numerator, float denominator) {
    const float quotient = sourceFaceBoundUp(numerator / denominator);
    return sourceFaceBoundUp(quotient * 1.00000059604644775390625);
}

// Independently compiled producer and receiver determinants may contract differently.
bool sourceFaceDefinitelyDegenerate(vec2 edgeU, vec2 edgeV, float determinant) {
    if (!sourceFaceBoundNormalOrZero(edgeU.x) || !sourceFaceBoundNormalOrZero(edgeU.y) ||
        !sourceFaceBoundNormalOrZero(edgeV.x) || !sourceFaceBoundNormalOrZero(edgeV.y) ||
        !sourceFaceBoundNormalOrZero(determinant)) return false;
    const vec2 absoluteU = abs(edgeU);
    const vec2 absoluteV = abs(edgeV);
    const float sumU = sourceFaceBoundUp(absoluteU.x + absoluteU.y);
    const float sumV = sourceFaceBoundUp(absoluteV.x + absoluteV.y);
    const float determinantMagnitude = sourceFaceBoundUp(
        sourceFaceBoundUp(absoluteU.x * absoluteV.y) +
        sourceFaceBoundUp(absoluteU.y * absoluteV.x));
    const float relativeError = uintBitsToFloat(0x35800009u);
    const float absoluteError = sourceFaceBoundUp(
        sourceFaceBoundUp(32.0 * uintBitsToFloat(0x00800000u)) *
        sourceFaceBoundUp(1.0 + sourceFaceBoundUp(sumU + sumV)));
    const float error = sourceFaceBoundUp(
        sourceFaceBoundUp(relativeError * determinantMagnitude) + absoluteError);
    const float receiverMagnitude = sourceFaceBoundUp(abs(determinant) + error);
    return sourceFaceBoundFinite(receiverMagnitude) && receiverMagnitude < 0.000001;
}

bool sourceFaceCascadeArithmeticSafe(vec2 origin, vec2 cellSize) {
    const uint originX = floatBitsToUint(origin.x) & 0x7FFFFFFFu;
    const uint originY = floatBitsToUint(origin.y) & 0x7FFFFFFFu;
    const uint sizeX = floatBitsToUint(cellSize.x);
    const uint sizeY = floatBitsToUint(cellSize.y);
    return (originX == 0u || originX >= 0x00800000u) &&
           (originY == 0u || originY >= 0x00800000u) &&
           originX <= 0x5C800000u && originY <= 0x5C800000u &&
           sizeX >= 0x00800000u && sizeX <= 0x58800000u &&
           sizeY >= 0x00800000u && sizeY <= 0x58800000u;
}

// The interval encloses receiver coordinates before floor and integer conversion.
vec2 sourceFaceTileInterval(float low, float high, float origin, float cellSize) {
    const float minimumNormal = uintBitsToFloat(0x00800000u);
    const float maximumFinite = uintBitsToFloat(0x7F7FFFFFu);
    const float differenceLow = sourceFaceBoundDown(low - origin);
    const float differenceHigh = sourceFaceBoundUp(high - origin);
    if (!sourceFaceBoundFinite(differenceLow) || !sourceFaceBoundFinite(differenceHigh))
        return vec2(-maximumFinite, maximumFinite);
    const float differenceMagnitude = max(abs(differenceLow), abs(differenceHigh));
    const float subtractionError = sourceFaceBoundUp(
        sourceFaceBoundUp(0.000000059604644775390625 * differenceMagnitude) +
        3.0 * minimumNormal);
    const float numeratorLow = sourceFaceBoundDown(differenceLow - subtractionError);
    const float numeratorHigh = sourceFaceBoundUp(differenceHigh + subtractionError);
    const float quotientLow = numeratorLow / cellSize;
    const float quotientHigh = numeratorHigh / cellSize;
    if (!sourceFaceBoundFinite(quotientLow) || !sourceFaceBoundFinite(quotientHigh))
        return vec2(-maximumFinite, maximumFinite);
    const float quotientMagnitude = max(abs(quotientLow), abs(quotientHigh));
    const float oneDivisionError = sourceFaceBoundUp(
        sourceFaceBoundUp(0.000000476837158203125 * quotientMagnitude) + minimumNormal);
    const float divisionError = sourceFaceBoundUp(2.0 * sourceFaceBoundUp(
        oneDivisionError * 1.00000059604644775390625));
    const vec2 interval = vec2(sourceFaceBoundDown(quotientLow - divisionError),
                               sourceFaceBoundUp(quotientHigh + divisionError));
    if (!sourceFaceBoundFinite(interval.x) || !sourceFaceBoundFinite(interval.y))
        return vec2(-maximumFinite, maximumFinite);
    return interval;
}

// False requests full-cascade indexing. The arithmetic contract and padding
// derivation are in docs/design/sun-face-index-coverage.md.
bool sourceFaceIndexBounds(vec2 corner, vec2 edgeU, vec2 edgeV, float determinant,
                           out vec2 low, out vec2 high) {
    if (!sourceFaceBoundNormalOrZero(corner.x) || !sourceFaceBoundNormalOrZero(corner.y) ||
        !sourceFaceBoundNormalOrZero(edgeU.x) || !sourceFaceBoundNormalOrZero(edgeU.y) ||
        !sourceFaceBoundNormalOrZero(edgeV.x) || !sourceFaceBoundNormalOrZero(edgeV.y) ||
        !sourceFaceBoundNormalOrZero(determinant)) return false;

    const vec2 absoluteCorner = abs(corner);
    const vec2 absoluteU = abs(edgeU);
    const vec2 absoluteV = abs(edgeV);
    const float maximumInput = uintBitsToFloat(0x5D800000u);
    if (max(absoluteCorner.x, absoluteCorner.y) > maximumInput ||
        max(absoluteU.x, absoluteU.y) > maximumInput ||
        max(absoluteV.x, absoluteV.y) > maximumInput) return false;
    const float cornerExtent = max(absoluteCorner.x, absoluteCorner.y);
    const float extentU = max(absoluteU.x, absoluteU.y);
    const float extentV = max(absoluteV.x, absoluteV.y);
    const float sumU = sourceFaceBoundUp(absoluteU.x + absoluteU.y);
    const float sumV = sourceFaceBoundUp(absoluteV.x + absoluteV.y);
    const float edgeExtent = sourceFaceBoundUp(extentU + extentV);
    const float radius = sourceFaceBoundUp(cornerExtent + edgeExtent);
    const float determinantMagnitude = sourceFaceBoundUp(
        sourceFaceBoundUp(absoluteU.x * absoluteV.y) +
        sourceFaceBoundUp(absoluteU.y * absoluteV.x));
    const float coupling = sourceFaceBoundUp(
        sourceFaceBoundUp(extentU * sumV) + sourceFaceBoundUp(extentV * sumU));
    const float determinantTerm = sourceFaceBoundUp(edgeExtent * determinantMagnitude);

    // ceil_binary32(gamma(16)), with u = 2^-24.
    const float relativeError = uintBitsToFloat(0x35800009u);
    const float absoluteError = sourceFaceBoundUp(
        sourceFaceBoundUp(32.0 * uintBitsToFloat(0x00800000u)) *
        sourceFaceBoundUp(1.0 + sourceFaceBoundUp(sumU + sumV)));
    const float denominatorError = sourceFaceBoundUp(
        sourceFaceBoundUp(relativeError *
            sourceFaceBoundUp(determinantMagnitude + coupling)) + absoluteError);
    const float denominator = sourceFaceBoundDown(abs(determinant) - denominatorError);
    if (!sourceFaceBoundFinite(denominator) || denominator <= 0.0) return false;

    const float relativeNumerator = sourceFaceBoundUp(relativeError * sourceFaceBoundUp(
        sourceFaceBoundUp(sourceFaceBoundUp(radius + cornerExtent) * coupling) +
        determinantTerm));
    const float numerator = sourceFaceBoundUp(
        relativeNumerator + sourceFaceBoundUp(edgeExtent * absoluteError));
    const float padding = sourceFaceBoundDivideUp(numerator, denominator);
    if (!sourceFaceBoundFinite(padding)) return false;

    low = vec2(
        sourceFaceBoundDown(sourceFaceBoundDown(
            sourceFaceBoundDown(corner.x + min(0.0, edgeU.x)) + min(0.0, edgeV.x)) - padding),
        sourceFaceBoundDown(sourceFaceBoundDown(
            sourceFaceBoundDown(corner.y + min(0.0, edgeU.y)) + min(0.0, edgeV.y)) - padding));
    high = vec2(
        sourceFaceBoundUp(sourceFaceBoundUp(
            sourceFaceBoundUp(corner.x + max(0.0, edgeU.x)) + max(0.0, edgeV.x)) + padding),
        sourceFaceBoundUp(sourceFaceBoundUp(
            sourceFaceBoundUp(corner.y + max(0.0, edgeU.y)) + max(0.0, edgeV.y)) + padding));
    return sourceFaceBoundFinite(low.x) && sourceFaceBoundFinite(low.y) &&
           sourceFaceBoundFinite(high.x) && sourceFaceBoundFinite(high.y);
}

#endif
