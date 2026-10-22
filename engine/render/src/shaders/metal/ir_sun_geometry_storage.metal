#ifndef IR_SUN_GEOMETRY_STORAGE_INCLUDED
#define IR_SUN_GEOMETRY_STORAGE_INCLUDED
#include "ir_sun_hierarchy_bounds.metal"

inline SourceSunGeometry readSourceSunGeometry(device const uint* sunDepthBuf, uint id) {
    const uint record = kSourceFaceRecordOffset + id * kSourceFaceRecordWords;
    return SourceSunGeometry{
        float3(as_type<float>(sunDepthBuf[record]), as_type<float>(sunDepthBuf[record + 1u]), as_type<float>(sunDepthBuf[record + 2u])),
        float3(as_type<float>(sunDepthBuf[record + 3u]), as_type<float>(sunDepthBuf[record + 4u]), as_type<float>(sunDepthBuf[record + 5u])),
        float3(as_type<float>(sunDepthBuf[record + 6u]), as_type<float>(sunDepthBuf[record + 7u]), as_type<float>(sunDepthBuf[record + 8u])),
        sunDepthBuf[record + 9u]};
}

// Word loads avoid imposing vector alignment on the appended SSBO range.
inline SourceSunBounds readSourceSunBounds(device const uint* sunDepthBuf, uint node) {
    const uint offset = kSourceFaceHierarchyOffset + node * kSourceFaceHierarchyNodeWords;
    SourceSunBounds bounds;
    for (uint axis = 0u; axis < 4u; ++axis) {
        bounds.low[axis] = as_type<float>(sunDepthBuf[offset + axis]);
        bounds.high[axis] = as_type<float>(sunDepthBuf[offset + 4u + axis]);
    }
    return bounds;
}
#endif
