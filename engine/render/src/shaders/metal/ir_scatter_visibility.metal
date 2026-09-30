#ifndef IR_SCATTER_VISIBILITY_INCLUDED
#define IR_SCATTER_VISIBILITY_INCLUDED

inline uint scatterVisibilityCode(float depth) {
    return uint(clamp(depth, 0.0, 1.0) * 16777216.0);
}

// D24 ties can straddle adjacent bins; retain the boundary halo as well as
// exact ties so the existing depth test and draw order still own arbitration.
inline bool scatterVisibilityReject(uint code, uint winner) {
    return code > winner && code - winner > 2u;
}

#endif
