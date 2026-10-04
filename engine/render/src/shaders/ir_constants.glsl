const int VOXEL_CHUNK_SIZE = 256;
const int kInvalidDepth = 0x7FFFFFFF;

// Must match this backend's voxel stage layouts;
// ir_voxel_dispatch owns the matching indirect-grid and lane-recovery math.
const int kStageMicroSlicesPerGroup = 8;
