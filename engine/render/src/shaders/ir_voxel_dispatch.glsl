// One sample domain owns visible, shadow-feeder and per-axis dispatches.
int voxelDispatchMicroSliceCount(int mode, int subdivisions, int perAxisRoute) {
    int edge = max(subdivisions, 1);
    return mode != 0 && perAxisRoute == 0 ? edge * edge : 1;
}

// Each OpenGL XY workgroup owns one voxel, including low-density work.
uint voxelDispatchVoxelsPerGroup(uint microSliceCount) {
    return 1u;
}

uvec2 voxelDispatchLane(uint groupIndex, uint groupZ, uint localZ, uint microSliceCount) {
    return uvec2(groupIndex, groupZ * uint(kStageMicroSlicesPerGroup) + localZ);
}
