#ifndef IR_SOURCE_FACE_DEPTH_INCLUDED
#define IR_SOURCE_FACE_DEPTH_INCLUDED

// Bit tests remain meaningful with fast-math enabled; a nonfinite producer
// must never certify a canvas for interpolation-only framebuffer depth.
inline bool sourceFaceFinite(float value) {
    return (as_type<uint>(value) & 0x7f800000u) != 0x7f800000u;
}

inline bool sourceFacePositionFinite(float3 position) {
    return sourceFaceFinite(position.x) && sourceFaceFinite(position.y) && sourceFaceFinite(position.z);
}

inline uint sourceFacePositionMagnitudeBits(float3 position) {
    const float3 magnitude = abs(position);
    return as_type<uint>(max(magnitude.x, max(magnitude.y, magnitude.z)));
}

inline bool sourceCanvasEarlyDepth(uint maxAbsPositionBits, uint maxPriority, uint invalid,
                           float4 rotation, float density, float depthScale,
                           int distanceOffset, int depthPriorityMode,
                           int minDepth, int maxDepth) {
    if (maxPriority != 0u || invalid != 0u || depthPriorityMode != 0 ||
        maxAbsPositionBits >= 0x7f800000u ||
        !sourceFacePositionFinite(rotation.xyz) || !sourceFaceFinite(rotation.w) ||
        !sourceFaceFinite(density) || !sourceFaceFinite(depthScale) ||
        density <= 0.0 || depthScale <= 0.0) return false;

    // Every corner is at most maxAbsPosition + 0.5 on each model axis.
    // For rotateByQuat(p,q) = p + 2*w*cross(u,p) + 2*cross(u,cross(u,p)),
    // S=|u|_1 bounds |sum(rotate(p,q))| by 3*r*(1+2*S*(S+|w|)).
    // Twice that enclosure, a full-cell radius and four extra depth codes
    // leave routing slack for rounded arithmetic. No unit-quaternion premise,
    // framebuffer bias or shadow bias is introduced. Overflow falls back.
    const float radius = as_type<float>(maxAbsPositionBits) + 1.0;
    const float sum = abs(rotation.x) + abs(rotation.y) + abs(rotation.z);
    const float bound = 6.0 * radius * (1.0 + 2.0 * sum * (sum + abs(rotation.w))) *
        density * float(kDepthEncodeShift) * depthScale + 4.0;
    if (!sourceFaceFinite(bound)) return false;
    const float low = float(distanceOffset) - bound;
    const float high = float(distanceOffset) + bound;
    // Whole-canvas selection preserves the source-face order. Foreground,
    // clamp-crossing and clip-crossing canvases keep their original shader.
    return false; // A/B control: route every source canvas through fragment depth.
}
#endif
