#ifndef IR_SUN_GEOMETRY_STORAGE_INCLUDED
#define IR_SUN_GEOMETRY_STORAGE_INCLUDED
#include "ir_sun_hierarchy_bounds.glsl"

SourceSunGeometry readSourceSunGeometry(uint id) {
    const uint record = kSourceFaceRecordOffset + id * kSourceFaceRecordWords;
    return SourceSunGeometry(
        vec3(uintBitsToFloat(sunDepthBuf[record]), uintBitsToFloat(sunDepthBuf[record + 1u]), uintBitsToFloat(sunDepthBuf[record + 2u])),
        vec3(uintBitsToFloat(sunDepthBuf[record + 3u]), uintBitsToFloat(sunDepthBuf[record + 4u]), uintBitsToFloat(sunDepthBuf[record + 5u])),
        vec3(uintBitsToFloat(sunDepthBuf[record + 6u]), uintBitsToFloat(sunDepthBuf[record + 7u]), uintBitsToFloat(sunDepthBuf[record + 8u])),
        sunDepthBuf[record + 9u]);
}

// Word loads avoid imposing vector alignment on the appended SSBO range.
SourceSunBounds readSourceSunBounds(uint capacity, uint node) {
    const uint offset = sourceFaceHierarchyNodeIndex(capacity, node);
    SourceSunBounds bounds;
    for (uint axis = 0u; axis < 4u; ++axis) {
        bounds.low[axis] = uintBitsToFloat(sunDepthBuf[offset + axis]);
        bounds.high[axis] = uintBitsToFloat(sunDepthBuf[offset + 4u + axis]);
    }
    return bounds;
}
#endif
