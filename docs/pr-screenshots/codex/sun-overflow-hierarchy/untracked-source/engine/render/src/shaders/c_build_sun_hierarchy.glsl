#version 450 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#include "ir_sun_face_query_layout.glsl"
layout(std430, binding = 28) buffer SunShadowDepthMap { uint sunDepthBuf[]; };
layout(std140, binding = 16) uniform SunHierarchyFrame { ivec4 hierarchyDispatch; };
#include "ir_sun_geometry_storage.glsl"

void writeSourceSunBounds(uint node, SourceSunBounds bounds) {
    const uint offset = uint(sunDepthBuf.length()) -
        (kSourceFaceBufferWords - kSourceFaceHierarchyOffset) + node * kSourceFaceHierarchyNodeWords;
    for (uint axis = 0u; axis < 4u; ++axis) {
        sunDepthBuf[offset + axis] = floatBitsToUint(bounds.low[axis]);
        sunDepthBuf[offset + 4u + axis] = floatBitsToUint(bounds.high[axis]);
    }
}

void main() {
    const uint id = gl_GlobalInvocationID.x;
    const uint records = sunDepthBuf[sourceFaceHeaderIndex(uint(sunDepthBuf.length()))];
    if (records == 0u || records > kSourceFaceCapacity ||
        sunDepthBuf[sourceFaceHierarchyNeededIndex(uint(sunDepthBuf.length()))] == 0u) return;
    const uint leaves = sourceSunHierarchyLeafCount(records);
    const uint level = uint(hierarchyDispatch.x);
    if (level == 0u) {
        if (id >= leaves) return;
        SourceSunBounds bounds = sourceSunEmptyBounds();
        const uint first = id * kSourceFaceHierarchyLeafSize;
        for (uint i = first; i < min(records, first + kSourceFaceHierarchyLeafSize); ++i)
            bounds = sourceSunMergeBounds(bounds, sourceSunGeometryBounds(readSourceSunGeometry(i)));
        writeSourceSunBounds(leaves + id, bounds);
    } else {
        if (level >= leaves || id >= level) return;
        const uint node = level + id;
        writeSourceSunBounds(node, sourceSunMergeBounds(readSourceSunBounds(node * 2u), readSourceSunBounds(node * 2u + 1u)));
    }
    // Consumers run only after the dispatch barrier; no intra-dispatch polling.
    if ((leaves == 1u && level == 0u) || level == 1u)
        sunDepthBuf[sourceFaceHierarchyReadyIndex(uint(sunDepthBuf.length()))] = leaves;
}
