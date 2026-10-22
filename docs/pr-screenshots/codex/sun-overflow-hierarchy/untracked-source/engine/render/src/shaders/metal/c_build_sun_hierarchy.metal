#include <metal_stdlib>
using namespace metal;
#include "ir_sun_face_query_layout.metal"
struct SunHierarchyFrame { int4 hierarchyDispatch; };
#include "ir_sun_geometry_storage.metal"

inline void writeSourceSunBounds(device uint* sunDepthBuf, uint node, SourceSunBounds bounds) {
    const uint offset = kSourceFaceHierarchyOffset + node * kSourceFaceHierarchyNodeWords;
    for (uint axis = 0u; axis < 4u; ++axis) {
        sunDepthBuf[offset + axis] = as_type<uint>(bounds.low[axis]);
        sunDepthBuf[offset + 4u + axis] = as_type<uint>(bounds.high[axis]);
    }
}

kernel void c_build_sun_hierarchy(device uint* sunDepthBuf [[buffer(28)]],
    constant SunHierarchyFrame& params [[buffer(16)]], uint id [[thread_position_in_grid]]) {
    const uint records = sunDepthBuf[kSourceFaceHeaderOffset];
    if (records == 0u || records > kSourceFaceCapacity ||
        sunDepthBuf[kSourceFaceHierarchyNeededOffset] == 0u) return;
    const uint leaves = sourceSunHierarchyLeafCount(records);
    const uint level = uint(params.hierarchyDispatch.x);
    if (level == 0u) {
        if (id >= leaves) return;
        SourceSunBounds bounds = sourceSunEmptyBounds();
        const uint first = id * kSourceFaceHierarchyLeafSize;
        for (uint i = first; i < min(records, first + kSourceFaceHierarchyLeafSize); ++i)
            bounds = sourceSunMergeBounds(bounds, sourceSunGeometryBounds(readSourceSunGeometry(sunDepthBuf, i)));
        writeSourceSunBounds(sunDepthBuf, leaves + id, bounds);
    } else {
        if (level >= leaves || id >= level) return;
        const uint node = level + id;
        writeSourceSunBounds(sunDepthBuf, node, sourceSunMergeBounds(readSourceSunBounds(sunDepthBuf, node * 2u), readSourceSunBounds(sunDepthBuf, node * 2u + 1u)));
    }
    // Consumers run only after the dispatch barrier; no intra-dispatch polling.
    if ((leaves == 1u && level == 0u) || level == 1u)
        sunDepthBuf[kSourceFaceHierarchyReadyOffset] = leaves;
}
