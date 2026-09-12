const int VOXEL_CHUNK_SIZE = 256;
const int kInvalidDepth = 0x7FFFFFFF;

// Micro-slice packing factor for the voxel→trixel stage dispatch. Both stage
// kernels raster `subdivisions²` micro-cells per visible voxel face; this
// constant packs that many micro-cells into the z-dimension of one workgroup
// (`local_size_z`), so the compact writes gz = ceil(subdivisions² / this) and
// each invocation recovers its flat slice as
// gl_WorkGroupID.z * this + gl_LocalInvocationID.z. The invocation set, and so
// the output, does not depend on this value; only the workgroup count does.
// MUST equal the `local_size_z` literal in c_voxel_to_trixel_stage_{1,2}_body.glsl,
// the Metal threadgroup-size map entries in metal_pipeline.cpp, and the divCeil in
// c_voxel_visibility_compact's writeDispatchDims — a mismatch drops or double-runs
// micro-slices.
const int kStageMicroSlicesPerGroup = 8;
