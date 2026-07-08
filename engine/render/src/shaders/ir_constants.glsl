// Shared GPU constants for all trixel pipeline shaders.
// Included via #include "ir_constants.glsl" (resolved by the engine's
// shader preprocessor at compile time).

const int VOXEL_CHUNK_SIZE = 256;
const int kInvalidDepth = 0x7FFFFFFF;

// Micro-slice packing factor for the voxel→trixel stage dispatch (#2258). Both
// stage kernels raster `subdivisions²` micro-cells per visible voxel face; this
// constant packs that many micro-cells into the z-dimension of one workgroup
// (`local_size_z`), so the compact writes gz = ceil(subdivisions² / this) and
// each invocation recovers its flat slice as
// gl_WorkGroupID.z * this + gl_LocalInvocationID.z. Same invocation set → the
// output is byte-identical; the launched workgroup count drops by this factor.
// Single source of truth: MUST equal the `local_size_z` literal in
// c_voxel_to_trixel_stage_{1,2}.glsl, the Metal threadgroup-size map entries in
// metal_pipeline.cpp, and the divCeil in c_voxel_visibility_compact's
// writeDispatchDims — a mismatch drops or double-runs micro-slices.
const int kStageMicroSlicesPerGroup = 8;
