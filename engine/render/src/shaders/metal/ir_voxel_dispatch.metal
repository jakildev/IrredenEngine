// Micro-slice sample domain of the visible and shadow-feeder voxel lattice.
inline int voxelDispatchMicroSliceCount(int mode, int subdivisions) {
    int edge = max(subdivisions, 1);
    return mode != 0 ? edge * edge : 1;
}

inline uint voxelDispatchVoxelsPerGroup(uint microSliceCount) {
    return max(uint(kStageMicroSlicesPerGroup) / microSliceCount, 1u);
}

// A partial voxel never straddles XY groups. Spare lanes in a group carry
// whole low-density voxels; the remaining tail is outside the sample domain.
inline uint2 voxelDispatchLane(uint groupIndex, uint groupZ, uint localZ, uint microSliceCount) {
    uint voxelsPerGroup = voxelDispatchVoxelsPerGroup(microSliceCount);
    uint lanesPerVoxel = min(microSliceCount, uint(kStageMicroSlicesPerGroup));
    uint voxelOffset = localZ / lanesPerVoxel;
    uint compactedIdx = groupIndex * voxelsPerGroup + voxelOffset;
    uint slice = groupZ * uint(kStageMicroSlicesPerGroup) + localZ % lanesPerVoxel;
    if (voxelOffset >= voxelsPerGroup) {
        slice = microSliceCount;
    }
    return uint2(compactedIdx, slice);
}

// Per-axis face lists launch one invocation per list entry, in 1-D groups of
// kPerAxisEntriesPerGroup; the group grid spills X into Y past 1024 groups.
// metal_pipeline.cpp's threadgroup size for the per-axis stage variants MUST
// equal this constant. The compact writes the dimensions and every per-axis
// consumer recovers its entry through these two helpers, so writer and readers
// share one layout.
constant uint kPerAxisEntriesPerGroup = 64u;

inline uint3 perAxisDispatchDims(uint entryCount) {
    uint groups = (entryCount + kPerAxisEntriesPerGroup - 1u) / kPerAxisEntriesPerGroup;
    uint gx = max(min(groups, 1024u), 1u);
    return uint3(gx, max((groups + gx - 1u) / gx, 1u), 1u);
}

inline uint perAxisDispatchEntry(uint2 groupId, uint groupsX, uint localIndex) {
    return (groupId.x + groupId.y * groupsX) * kPerAxisEntriesPerGroup + localIndex;
}

// The visible-triplet slot whose face lies on `axis`. The camera-visible
// triplet carries one face per axis, so a per-axis list entry has exactly one.
inline int perAxisVisibleSlot(int4 visibleFaces, int axis) {
    return (visibleFaces.x >> 1) == axis ? 0 : (visibleFaces.y >> 1) == axis ? 1 : 2;
}
