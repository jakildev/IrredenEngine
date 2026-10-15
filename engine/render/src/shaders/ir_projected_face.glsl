#ifndef IR_PROJECTED_FACE_INCLUDED
#define IR_PROJECTED_FACE_INCLUDED

float projectedFaceDeterminant(vec2 edgeU, vec2 edgeV) {
    return edgeU.x * edgeV.y - edgeU.y * edgeV.x;
}

// The caller rejects degenerate bases before dividing. Coverage ownership is
// separate: display faces are half-open; finite shadow footprints are closed.
vec2 projectedFaceCoordinates(vec2 delta, vec2 edgeU, vec2 edgeV, float determinant) {
    return vec2((delta.x * edgeV.y - delta.y * edgeV.x) / determinant,
                (edgeU.x * delta.y - edgeU.y * delta.x) / determinant);
}

// Conservative closed-edge predicates for independently rounded projected
// inputs and finite normal binary32 arithmetic. gamma(7) bounds input rounding,
// subtraction and two-product edge functions, not upstream projection error.
// The caller rejects degenerate bases and keeps uncertain-hit depth on the face.
bool projectedFaceContainsClosed(vec2 point, vec2 corner, vec2 edgeU, vec2 edgeV,
                                 float determinant, vec2 uv) {
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
