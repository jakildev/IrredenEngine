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

#endif
