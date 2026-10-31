#include <metal_stdlib>
using namespace metal;
#include "ir_sun_face_query_layout.metal"
struct SunHierarchyFrame { int4 hierarchyDispatch; };
#include "ir_sun_geometry_storage.metal"

inline void writeSourceSunBounds(device uint* sunDepthBuf, uint capacity, uint node, SourceSunBounds bounds) {
    const uint offset = sourceFaceHierarchyNodeIndex(capacity, node);
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
    threadgroup SourceSunBounds* hierarchyBounds, uint capacity, uint lane, uint pass, uint rootNode,
    uint width, uint leaves, uint records) {
    if (lane >= width) return;
    const uint node = rootNode * width + lane;
    SourceSunBounds bounds;
    if (pass == 0u) {
        bounds = sourceSunLeafBounds(sunDepthBuf, node - leaves, records);
        writeSourceSunBounds(sunDepthBuf, capacity, node, bounds);
    } else {
        bounds = readSourceSunBounds(sunDepthBuf, capacity, node);
    }
    hierarchyBounds[width + lane] = bounds;
}

inline void reduceSourceSunHierarchy(device uint* sunDepthBuf,
    threadgroup SourceSunBounds* hierarchyBounds, uint capacity, uint lane, uint level, uint rootNode) {
    if (lane >= level) return;
    const uint node = level + lane;
    const SourceSunBounds bounds = sourceSunMergeBounds(
        hierarchyBounds[node * 2u], hierarchyBounds[node * 2u + 1u]);
    hierarchyBounds[node] = bounds;
    writeSourceSunBounds(sunDepthBuf, capacity, rootNode * level + lane, bounds);
}

kernel void c_build_sun_hierarchy(device uint* sunDepthBuf [[buffer(28)]],
    constant SunHierarchyFrame& params [[buffer(16)]],
    uint lane [[thread_index_in_threadgroup]], uint3 groupId [[threadgroup_position_in_grid]]) {
    threadgroup SourceSunBounds hierarchyBounds[2u * kSourceFaceHierarchyWorkgroupSize];
    const uint group = groupId.x;
    const uint capacity = sunDepthBuf[kSourceFaceCapacityOffset];
    const uint records = sourceFaceRetainedRecords(sunDepthBuf[kSourceFaceHeaderOffset], capacity);
    if (records == 0u ||
        sunDepthBuf[sourceFaceHierarchyNeededIndex(capacity)] == 0u) return;
    const uint leaves = sourceSunHierarchyLeafCount(records);
    const uint pass = uint(params.hierarchyDispatch.x);
    // Pass p reduces the nodes its predecessors rooted: one workgroup-wide
    // subtree per group, until a single subtree reaches the root. Later passes
    // re-read the finished root, so every dispatched pass is group-uniform.
    uint level = leaves;
    for (uint built = 0u; built < pass; ++built) level /= min(level, kSourceFaceHierarchyWorkgroupSize);
    const uint width = min(level, kSourceFaceHierarchyWorkgroupSize);
    const uint subtrees = level / width;
    if (group >= subtrees) return;
    const uint rootNode = subtrees + group;
    // At local width W, lane i owns global node rootNode * W + i.
    initializeSourceSunHierarchy(sunDepthBuf, hierarchyBounds, capacity, lane, pass, rootNode, width, leaves, records);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint stage = width / 2u; stage > 0u; stage /= 2u) {
        reduceSourceSunHierarchy(sunDepthBuf, hierarchyBounds, capacity, lane, stage, rootNode);
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    // Passes and consumers are separated by dispatch storage barriers; the
    // first pass only roots subtrees, so only a later pass publishes.
    if (pass != 0u && subtrees == 1u && lane == 0u)
        sunDepthBuf[sourceFaceHierarchyReadyIndex(capacity)] = leaves;
}
