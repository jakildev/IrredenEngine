#include <metal_stdlib>
using namespace metal;
#include "ir_sun_face_query_layout.metal"
// x: build pass; y: leaves read records through the leaf order; z: a leaf-order
// pass (kSourceFaceHierarchyOrder*) instead of a build pass.
struct SunHierarchyFrame { int4 hierarchyDispatch; };
#include "ir_sun_geometry_storage.metal"

inline void writeSourceSunBounds(device uint* sunDepthBuf, uint capacity, uint node, SourceSunBounds bounds) {
    const uint offset = sourceFaceHierarchyNodeIndex(capacity, node);
    for (uint axis = 0u; axis < 4u; ++axis) {
        sunDepthBuf[offset + axis] = as_type<uint>(bounds.low[axis]);
        sunDepthBuf[offset + 4u + axis] = as_type<uint>(bounds.high[axis]);
    }
}

inline SourceSunBounds sourceSunLeafBounds(device const uint* sunDepthBuf, uint capacity, uint leaf,
    uint records, bool ordered) {
    SourceSunBounds bounds = sourceSunEmptyBounds();
    const uint first = leaf * kSourceFaceHierarchyLeafSize;
    for (uint i = first; i < min(records, first + kSourceFaceHierarchyLeafSize); ++i) {
        const uint record = ordered ? readSourceSunLeafRecord(sunDepthBuf, capacity, i) : i;
        bounds = sourceSunMergeBounds(bounds, sourceSunGeometryBounds(readSourceSunGeometry(sunDepthBuf, record)));
    }
    return bounds;
}

inline uint spreadMortonBits(uint value) {
    value = (value | (value << 16u)) & 0x030000FFu;
    value = (value | (value << 8u)) & 0x0300F00Fu;
    value = (value | (value << 4u)) & 0x030C30C3u;
    return (value | (value << 2u)) & 0x09249249u;
}

// XYZ Morton cell of a record's bounds centre within the unordered root. A NaN
// or out-of-root position clamps to an edge cell. Buckets only order leaves:
// any bucket, and any order within one, keeps every answer.
inline uint sourceSunOrderBucket(device const uint* sunDepthBuf, uint capacity, uint record) {
    const SourceSunBounds bounds = sourceSunGeometryBounds(readSourceSunGeometry(sunDepthBuf, record));
    if (bounds.low.w != 0.0) return 0u;
    const SourceSunBounds root = readSourceSunBounds(sunDepthBuf, capacity, 1u);
    uint bucket = 0u;
    for (uint axis = 0u; axis < 3u; ++axis) {
        const float position = (0.5 * (bounds.low[axis] + bounds.high[axis]) - root.low[axis]) /
            (root.high[axis] - root.low[axis]);
        const uint cell = position > 0.0
            ? min(uint(min(position, 1.0) * float(kSourceFaceHierarchyOrderCells)), kSourceFaceHierarchyOrderCells - 1u)
            : 0u;
        bucket |= spreadMortonBits(cell) << axis;
    }
    return bucket;
}

// Counting sort of record IDs by bucket. Count adds each record to its bucket
// (zeroed by the frame's clear); scatter claims a slot from the bucket's start.
inline void orderSourceSunRecord(device uint* sunDepthBuf, uint capacity, uint records, uint record,
    bool scatter) {
    if (record >= records) return;
    device atomic_uint* bucket = (device atomic_uint*)&sunDepthBuf[
        sourceFaceHierarchyOrderIndex(capacity) + sourceSunOrderBucket(sunDepthBuf, capacity, record)];
    const uint slot = atomic_fetch_add_explicit(bucket, 1u, memory_order_relaxed);
    if (scatter) sunDepthBuf[sourceFaceHierarchyOrderEntriesIndex(capacity) + slot] = record;
}

// The scan gives each lane a contiguous run of buckets.
inline uint sourceSunOrderRun(uint capacity, uint lane) {
    return sourceFaceHierarchyOrderIndex(capacity) +
        lane * (kSourceFaceHierarchyOrderBuckets / kSourceFaceHierarchyWorkgroupSize);
}

inline void sumSourceSunOrderRun(device const uint* sunDepthBuf, threadgroup uint* orderRunStarts,
    uint capacity, uint lane) {
    const uint first = sourceSunOrderRun(capacity, lane);
    uint total = 0u;
    for (uint i = 0u; i < kSourceFaceHierarchyOrderBuckets / kSourceFaceHierarchyWorkgroupSize; ++i)
        total += sunDepthBuf[first + i];
    orderRunStarts[lane] = total;
}

inline void prefixSourceSunOrderRuns(threadgroup uint* orderRunStarts, uint lane) {
    if (lane != 0u) return;
    uint start = 0u;
    for (uint run = 0u; run < kSourceFaceHierarchyWorkgroupSize; ++run) {
        const uint total = orderRunStarts[run];
        orderRunStarts[run] = start;
        start += total;
    }
}

inline void startSourceSunOrderRun(device uint* sunDepthBuf, threadgroup uint* orderRunStarts,
    uint capacity, uint lane) {
    const uint first = sourceSunOrderRun(capacity, lane);
    uint start = orderRunStarts[lane];
    for (uint i = 0u; i < kSourceFaceHierarchyOrderBuckets / kSourceFaceHierarchyWorkgroupSize; ++i) {
        const uint count = sunDepthBuf[first + i];
        sunDepthBuf[first + i] = start;
        start += count;
    }
}

inline void initializeSourceSunHierarchy(device uint* sunDepthBuf,
    threadgroup SourceSunBounds* hierarchyBounds, uint capacity, uint lane, uint pass, uint rootNode,
    uint width, uint leaves, uint records, bool ordered) {
    if (lane >= width) return;
    const uint node = rootNode * width + lane;
    SourceSunBounds bounds;
    if (pass == 0u) {
        bounds = sourceSunLeafBounds(sunDepthBuf, capacity, node - leaves, records, ordered);
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
    uint lane [[thread_index_in_threadgroup]], uint3 groupId [[threadgroup_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]]) {
    threadgroup SourceSunBounds hierarchyBounds[2u * kSourceFaceHierarchyWorkgroupSize];
    threadgroup uint orderRunStarts[kSourceFaceHierarchyWorkgroupSize];
    const uint group = groupId.x + groupId.y * groupCount.x;
    const uint capacity = sunDepthBuf[kSourceFaceCapacityOffset];
    const uint records = sourceFaceRetainedRecords(sunDepthBuf[kSourceFaceHeaderOffset], capacity);
    if (records == 0u ||
        sunDepthBuf[sourceFaceHierarchyNeededIndex(capacity)] == 0u) return;
    const uint order = uint(params.hierarchyDispatch.z);
    if (order == kSourceFaceHierarchyOrderScan) {
        if (group != 0u) return;
        sumSourceSunOrderRun(sunDepthBuf, orderRunStarts, capacity, lane);
        threadgroup_barrier(mem_flags::mem_threadgroup);
        prefixSourceSunOrderRuns(orderRunStarts, lane);
        threadgroup_barrier(mem_flags::mem_threadgroup);
        startSourceSunOrderRun(sunDepthBuf, orderRunStarts, capacity, lane);
        return;
    }
    if (order != 0u) {
        orderSourceSunRecord(sunDepthBuf, capacity, records, group * kSourceFaceHierarchyWorkgroupSize + lane,
            order == kSourceFaceHierarchyOrderScatter);
        return;
    }
    const bool ordered = params.hierarchyDispatch.y != 0;
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
    initializeSourceSunHierarchy(sunDepthBuf, hierarchyBounds, capacity, lane, pass, rootNode, width, leaves,
        records, ordered);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint stage = width / 2u; stage > 0u; stage /= 2u) {
        reduceSourceSunHierarchy(sunDepthBuf, hierarchyBounds, capacity, lane, stage, rootNode);
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    // Passes and consumers are separated by dispatch storage barriers; the
    // first pass only roots subtrees, so only a later pass publishes, and only
    // the ordered build is queryable.
    if (ordered && pass != 0u && subtrees == 1u && lane == 0u)
        sunDepthBuf[sourceFaceHierarchyReadyIndex(capacity)] = leaves;
}
