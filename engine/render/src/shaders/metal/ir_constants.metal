constant int VOXEL_CHUNK_SIZE = 256;
constant int kInvalidDepth = 0x7FFFFFFF;

// Must match this backend's voxel stage threadgroup registry;
// ir_voxel_dispatch owns the matching indirect-grid and lane-recovery math.
constant int kStageMicroSlicesPerGroup = 32;
