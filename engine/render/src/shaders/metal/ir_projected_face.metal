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
