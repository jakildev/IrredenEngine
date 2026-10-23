#version 450 core
layout(local_size_x = 128, local_size_y = 1, local_size_z = 1) in;
#include "ir_sun_face_query_layout.glsl"
layout(std430, binding = 28) buffer SunShadowDepthMap { uint sunDepthBuf[]; };
layout(std140, binding = 16) uniform SunHierarchyFrame { ivec4 hierarchyDispatch; };
#include "ir_sun_geometry_storage.glsl"

shared SourceSunBounds hierarchyBounds[2u * kSourceFaceHierarchyWorkgroupSize];

void writeSourceSunBounds(uint node, SourceSunBounds bounds) {
    const uint offset = uint(sunDepthBuf.length()) -
        (kSourceFaceBufferWords - kSourceFaceHierarchyOffset) + node * kSourceFaceHierarchyNodeWords;
    for (uint axis = 0u; axis < 4u; ++axis) {
        sunDepthBuf[offset + axis] = floatBitsToUint(bounds.low[axis]);
        sunDepthBuf[offset + 4u + axis] = floatBitsToUint(bounds.high[axis]);
    }
}

SourceSunBounds sourceSunLeafBounds(uint leaf, uint records) {
    SourceSunBounds bounds = sourceSunEmptyBounds();
    const uint first = leaf * kSourceFaceHierarchyLeafSize;
    for (uint i = first; i < min(records, first + kSourceFaceHierarchyLeafSize); ++i)
        bounds = sourceSunMergeBounds(bounds, sourceSunGeometryBounds(readSourceSunGeometry(i)));
    return bounds;
}

void initializeSourceSunHierarchy(uint lane, uint pass, uint rootNode, uint width,
    uint leaves, uint records) {
    if (lane >= width) return;
    const uint node = rootNode * width + lane;
    SourceSunBounds bounds;
    if (pass == 0u) {
        bounds = sourceSunLeafBounds(node - leaves, records);
        writeSourceSunBounds(node, bounds);
    } else {
        bounds = readSourceSunBounds(node);
    }
    hierarchyBounds[width + lane] = bounds;
}

void reduceSourceSunHierarchy(uint lane, uint level, uint rootNode) {
    if (lane >= level) return;
    const uint node = level + lane;
    const SourceSunBounds bounds = sourceSunMergeBounds(
        hierarchyBounds[node * 2u], hierarchyBounds[node * 2u + 1u]);
    hierarchyBounds[node] = bounds;
    writeSourceSunBounds(rootNode * level + lane, bounds);
}

void main() {
    const uint lane = gl_LocalInvocationID.x;
    const uint group = gl_WorkGroupID.x;
    const uint records = sunDepthBuf[sourceFaceHeaderIndex(uint(sunDepthBuf.length()))];
    if (records == 0u || records > kSourceFaceCapacity ||
        sunDepthBuf[sourceFaceHierarchyNeededIndex(uint(sunDepthBuf.length()))] == 0u) return;
    const uint leaves = sourceSunHierarchyLeafCount(records);
    const uint pass = uint(hierarchyDispatch.x);
    const uint subtreeLeaves = min(leaves, kSourceFaceHierarchyWorkgroupSize);
    const uint subtrees = leaves / subtreeLeaves;
    // Every kernel return is group-uniform; inactive lanes still reach each barrier.
    if (pass >= kSourceFaceHierarchyBuildPasses ||
        (pass == 0u ? group >= subtrees : group != 0u)) return;
    const uint width = pass == 0u ? subtreeLeaves : subtrees;
    const uint rootNode = pass == 0u ? subtrees + group : 1u;
    // At local width W, lane i owns global node rootNode * W + i.
    initializeSourceSunHierarchy(lane, pass, rootNode, width, leaves, records);
    barrier();
    for (uint level = width / 2u; level > 0u; level /= 2u) {
        reduceSourceSunHierarchy(lane, level, rootNode);
        barrier();
    }
    // Both passes and consumers are separated by dispatch storage barriers.
    if (pass == 1u && lane == 0u)
        sunDepthBuf[sourceFaceHierarchyReadyIndex(uint(sunDepthBuf.length()))] = leaves;
}
