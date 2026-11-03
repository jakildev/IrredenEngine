#ifndef IR_SUN_HIERARCHY_BOUNDS_INCLUDED
#define IR_SUN_HIERARCHY_BOUNDS_INCLUDED
#include "ir_sun_face_bounds.metal"
#include "ir_sun_face_query_layout.metal"

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
