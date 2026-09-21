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

#endif
