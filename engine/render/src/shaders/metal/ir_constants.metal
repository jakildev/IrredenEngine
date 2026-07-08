// Shared GPU constants for all trixel pipeline Metal shaders.
// Mirrors shaders/ir_constants.glsl.

constant int VOXEL_CHUNK_SIZE = 256;
constant int kInvalidDepth = 0x7FFFFFFF;

// Micro-slice packing factor for the voxel→trixel stage dispatch (#2258).
// Mirrors shaders/ir_constants.glsl — must equal the Metal threadgroup-size map
// entries for c_voxel_to_trixel_stage_{1,2} in metal_pipeline.cpp and the
// divCeil in c_voxel_visibility_compact's writeDispatchDims.
constant int kStageMicroSlicesPerGroup = 8;
