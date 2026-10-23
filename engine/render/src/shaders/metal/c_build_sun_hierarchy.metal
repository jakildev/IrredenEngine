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

inline SourceSunBounds sourceSunLeafBounds(device const uint* sunDepthBuf, uint leaf, uint records) {
    SourceSunBounds bounds = sourceSunEmptyBounds();
    const uint first = leaf * kSourceFaceHierarchyLeafSize;
    for (uint i = first; i < min(records, first + kSourceFaceHierarchyLeafSize); ++i)
        bounds = sourceSunMergeBounds(bounds, sourceSunGeometryBounds(readSourceSunGeometry(sunDepthBuf, i)));
    return bounds;
}

inline void initializeSourceSunHierarchy(device uint* sunDepthBuf,
    threadgroup SourceSunBounds* hierarchyBounds, uint lane, uint pass, uint rootNode,
    uint width, uint leaves, uint records) {
    if (lane >= width) return;
    const uint node = rootNode * width + lane;
    SourceSunBounds bounds;
    if (pass == 0u) {
        bounds = sourceSunLeafBounds(sunDepthBuf, node - leaves, records);
        writeSourceSunBounds(sunDepthBuf, node, bounds);
    } else {
        bounds = readSourceSunBounds(sunDepthBuf, node);
    }
    hierarchyBounds[width + lane] = bounds;
}

inline void reduceSourceSunHierarchy(device uint* sunDepthBuf,
    threadgroup SourceSunBounds* hierarchyBounds, uint lane, uint level, uint rootNode) {
    if (lane >= level) return;
    const uint node = level + lane;
    const SourceSunBounds bounds = sourceSunMergeBounds(
        hierarchyBounds[node * 2u], hierarchyBounds[node * 2u + 1u]);
    hierarchyBounds[node] = bounds;
    writeSourceSunBounds(sunDepthBuf, rootNode * level + lane, bounds);
}

kernel void c_build_sun_hierarchy(device uint* sunDepthBuf [[buffer(28)]],
    constant SunHierarchyFrame& params [[buffer(16)]],
    uint lane [[thread_index_in_threadgroup]], uint3 groupId [[threadgroup_position_in_grid]]) {
    threadgroup SourceSunBounds hierarchyBounds[2u * kSourceFaceHierarchyWorkgroupSize];
    const uint group = groupId.x;
    const uint records = sunDepthBuf[kSourceFaceHeaderOffset];
    if (records == 0u || records > kSourceFaceCapacity ||
        sunDepthBuf[kSourceFaceHierarchyNeededOffset] == 0u) return;
    const uint leaves = sourceSunHierarchyLeafCount(records);
    const uint pass = uint(params.hierarchyDispatch.x);
    const uint subtreeLeaves = min(leaves, kSourceFaceHierarchyWorkgroupSize);
    const uint subtrees = leaves / subtreeLeaves;
    // Every kernel return is group-uniform; inactive lanes still reach each barrier.
    if (pass >= kSourceFaceHierarchyBuildPasses ||
        (pass == 0u ? group >= subtrees : group != 0u)) return;
    const uint width = pass == 0u ? subtreeLeaves : subtrees;
    const uint rootNode = pass == 0u ? subtrees + group : 1u;
    // At local width W, lane i owns global node rootNode * W + i.
    initializeSourceSunHierarchy(sunDepthBuf, hierarchyBounds, lane, pass, rootNode, width, leaves, records);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint level = width / 2u; level > 0u; level /= 2u) {
        reduceSourceSunHierarchy(sunDepthBuf, hierarchyBounds, lane, level, rootNode);
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    // Both passes and consumers are separated by dispatch storage barriers.
    if (pass == 1u && lane == 0u)
        sunDepthBuf[kSourceFaceHierarchyReadyOffset] = leaves;
}
