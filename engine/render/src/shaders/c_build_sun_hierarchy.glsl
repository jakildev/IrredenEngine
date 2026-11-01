#version 450 core
layout(local_size_x = 128, local_size_y = 1, local_size_z = 1) in;
#include "ir_sun_face_query_layout.glsl"
layout(std430, binding = 28) buffer SunShadowDepthMap { uint sunDepthBuf[]; };
// x: build pass; y: leaves read records through the leaf order; z: a leaf-order
// pass (kSourceFaceHierarchyOrder*) instead of a build pass.
layout(std140, binding = 16) uniform SunHierarchyFrame { ivec4 hierarchyDispatch; };
#include "ir_sun_geometry_storage.glsl"

shared SourceSunBounds hierarchyBounds[2u * kSourceFaceHierarchyWorkgroupSize];
shared uint orderRunStarts[kSourceFaceHierarchyWorkgroupSize];

void writeSourceSunBounds(uint capacity, uint node, SourceSunBounds bounds) {
    const uint offset = sourceFaceHierarchyNodeIndex(capacity, node);
    for (uint axis = 0u; axis < 4u; ++axis) {
        sunDepthBuf[offset + axis] = floatBitsToUint(bounds.low[axis]);
        sunDepthBuf[offset + 4u + axis] = floatBitsToUint(bounds.high[axis]);
    }
}

SourceSunBounds sourceSunLeafBounds(uint capacity, uint leaf, uint records, bool ordered) {
    SourceSunBounds bounds = sourceSunEmptyBounds();
    const uint first = leaf * kSourceFaceHierarchyLeafSize;
    for (uint i = first; i < min(records, first + kSourceFaceHierarchyLeafSize); ++i) {
        const uint record = ordered ? readSourceSunLeafRecord(capacity, i) : i;
        bounds = sourceSunMergeBounds(bounds, sourceSunGeometryBounds(readSourceSunGeometry(record)));
    }
    return bounds;
}

uint spreadMortonBits(uint value) {
    value = (value | (value << 16u)) & 0x030000FFu;
    value = (value | (value << 8u)) & 0x0300F00Fu;
    value = (value | (value << 4u)) & 0x030C30C3u;
    return (value | (value << 2u)) & 0x09249249u;
}

// XYZ Morton cell of a record's bounds centre within the unordered root. A NaN
// or out-of-root position clamps to an edge cell. Buckets only order leaves:
// any bucket, and any order within one, keeps every answer.
uint sourceSunOrderBucket(uint capacity, uint record) {
    const SourceSunBounds bounds = sourceSunGeometryBounds(readSourceSunGeometry(record));
    if (bounds.low.w != 0.0) return 0u;
    const SourceSunBounds root = readSourceSunBounds(capacity, 1u);
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
void orderSourceSunRecord(uint capacity, uint records, uint record, bool scatter) {
    if (record >= records) return;
    const uint slot = atomicAdd(
        sunDepthBuf[sourceFaceHierarchyOrderIndex(capacity) + sourceSunOrderBucket(capacity, record)], 1u);
    if (scatter) sunDepthBuf[sourceFaceHierarchyOrderEntriesIndex(capacity) + slot] = record;
}

// The scan gives each lane a contiguous run of buckets.
uint sourceSunOrderRun(uint capacity, uint lane) {
    return sourceFaceHierarchyOrderIndex(capacity) +
        lane * (kSourceFaceHierarchyOrderBuckets / kSourceFaceHierarchyWorkgroupSize);
}

void sumSourceSunOrderRun(uint capacity, uint lane) {
    const uint first = sourceSunOrderRun(capacity, lane);
    uint total = 0u;
    for (uint i = 0u; i < kSourceFaceHierarchyOrderBuckets / kSourceFaceHierarchyWorkgroupSize; ++i)
        total += sunDepthBuf[first + i];
    orderRunStarts[lane] = total;
}

void prefixSourceSunOrderRuns(uint lane) {
    if (lane != 0u) return;
    uint start = 0u;
    for (uint run = 0u; run < kSourceFaceHierarchyWorkgroupSize; ++run) {
        const uint total = orderRunStarts[run];
        orderRunStarts[run] = start;
        start += total;
    }
}

void startSourceSunOrderRun(uint capacity, uint lane) {
    const uint first = sourceSunOrderRun(capacity, lane);
    uint start = orderRunStarts[lane];
    for (uint i = 0u; i < kSourceFaceHierarchyOrderBuckets / kSourceFaceHierarchyWorkgroupSize; ++i) {
        const uint count = sunDepthBuf[first + i];
        sunDepthBuf[first + i] = start;
        start += count;
    }
}

void initializeSourceSunHierarchy(uint capacity, uint lane, uint pass, uint rootNode, uint width,
    uint leaves, uint records, bool ordered) {
    if (lane >= width) return;
    const uint node = rootNode * width + lane;
    SourceSunBounds bounds;
    if (pass == 0u) {
        bounds = sourceSunLeafBounds(capacity, node - leaves, records, ordered);
        writeSourceSunBounds(capacity, node, bounds);
    } else {
        bounds = readSourceSunBounds(capacity, node);
    }
    hierarchyBounds[width + lane] = bounds;
}

void reduceSourceSunHierarchy(uint capacity, uint lane, uint level, uint rootNode) {
    if (lane >= level) return;
    const uint node = level + lane;
    const SourceSunBounds bounds = sourceSunMergeBounds(
        hierarchyBounds[node * 2u], hierarchyBounds[node * 2u + 1u]);
    hierarchyBounds[node] = bounds;
    writeSourceSunBounds(capacity, rootNode * level + lane, bounds);
}

void main() {
    const uint lane = gl_LocalInvocationID.x;
    const uint group = gl_WorkGroupID.x;
    const uint boundWords = uint(sunDepthBuf.length());
    const uint capacity = sourceFaceCapacity(boundWords);
    const uint records = sourceFaceRetainedRecords(sunDepthBuf[sourceFaceHeaderIndex(boundWords)], capacity);
    if (records == 0u ||
        sunDepthBuf[sourceFaceHierarchyNeededIndex(capacity)] == 0u) return;
    const uint order = uint(hierarchyDispatch.z);
    if (order == kSourceFaceHierarchyOrderScan) {
        if (group != 0u) return;
        sumSourceSunOrderRun(capacity, lane);
        barrier();
        prefixSourceSunOrderRuns(lane);
        barrier();
        startSourceSunOrderRun(capacity, lane);
        return;
    }
    if (order != 0u) {
        orderSourceSunRecord(capacity, records, group * kSourceFaceHierarchyWorkgroupSize + lane,
            order == kSourceFaceHierarchyOrderScatter);
        return;
    }
    const bool ordered = hierarchyDispatch.y != 0;
    const uint leaves = sourceSunHierarchyLeafCount(records);
    const uint pass = uint(hierarchyDispatch.x);
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
    initializeSourceSunHierarchy(capacity, lane, pass, rootNode, width, leaves, records, ordered);
    barrier();
    for (uint stage = width / 2u; stage > 0u; stage /= 2u) {
        reduceSourceSunHierarchy(capacity, lane, stage, rootNode);
        barrier();
    }
    // Passes and consumers are separated by dispatch storage barriers; the
    // first pass only roots subtrees, so only a later pass publishes, and only
    // the ordered build is queryable.
    if (ordered && pass != 0u && subtrees == 1u && lane == 0u)
        sunDepthBuf[sourceFaceHierarchyReadyIndex(capacity)] = leaves;
}
