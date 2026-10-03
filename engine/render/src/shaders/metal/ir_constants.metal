constant int VOXEL_CHUNK_SIZE = 256;
constant int kInvalidDepth = 0x7FFFFFFF;

// Must match the voxel stage GLSL layouts and Metal threadgroup registry;
// ir_voxel_dispatch owns the matching indirect-grid and lane-recovery math.
constant int kStageMicroSlicesPerGroup = 32;
