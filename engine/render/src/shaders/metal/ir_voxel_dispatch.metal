// One sample domain owns visible, shadow-feeder and per-axis dispatches.
inline int voxelDispatchMicroSliceCount(int mode, int subdivisions, int perAxisRoute) {
    int edge = max(subdivisions, 1);
    return mode != 0 && perAxisRoute == 0 ? edge * edge : 1;
}

inline uint voxelDispatchVoxelsPerGroup(uint microSliceCount) {
    return max(uint(kStageMicroSlicesPerGroup) / microSliceCount, 1u);
}

// A partial voxel never straddles XY groups. Spare lanes in a group carry
// whole low-density voxels; the remaining tail is outside the sample domain.
inline uint2 voxelDispatchLane(uint groupIndex, uint groupZ, uint localZ, uint microSliceCount) {
    if (microSliceCount >= uint(kStageMicroSlicesPerGroup)) {
        return uint2(groupIndex, groupZ * uint(kStageMicroSlicesPerGroup) + localZ);
    }
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
