// Micro-slice sample domain of the visible and shadow-feeder voxel lattice.
int voxelDispatchMicroSliceCount(int mode, int subdivisions) {
    int edge = max(subdivisions, 1);
    return mode != 0 ? edge * edge : 1;
}

// Each OpenGL XY workgroup owns one voxel, including low-density work.
// Both helpers retain microSliceCount to share Metal's call signature.
uint voxelDispatchVoxelsPerGroup(uint microSliceCount) {
    return 1u;
}

uvec2 voxelDispatchLane(uint groupIndex, uint groupZ, uint localZ, uint microSliceCount) {
    return uvec2(groupIndex, groupZ * uint(kStageMicroSlicesPerGroup) + localZ);
}

// Per-axis face lists launch one invocation per list entry, in 1-D groups of
// kPerAxisEntriesPerGroup; the group grid spills X into Y past 1024 groups.
// The per-axis stage variants' local_size_x literal MUST equal this constant.
// The compact writes the dimensions and every per-axis consumer recovers its
// entry through these two helpers, so writer and readers share one layout.
const uint kPerAxisEntriesPerGroup = 64u;

uvec3 perAxisDispatchDims(uint entryCount) {
    uint groups = (entryCount + kPerAxisEntriesPerGroup - 1u) / kPerAxisEntriesPerGroup;
    uint gx = max(min(groups, 1024u), 1u);
    return uvec3(gx, max((groups + gx - 1u) / gx, 1u), 1u);
}

uint perAxisDispatchEntry(uvec2 groupId, uint groupsX, uint localIndex) {
    return (groupId.x + groupId.y * groupsX) * kPerAxisEntriesPerGroup + localIndex;
}

// The visible-triplet slot whose face lies on `axis`. The camera-visible
// triplet carries one face per axis, so a per-axis list entry has exactly one.
int perAxisVisibleSlot(ivec4 visibleFaces, int axis) {
    return (visibleFaces.x >> 1) == axis ? 0 : (visibleFaces.y >> 1) == axis ? 1 : 2;
}
