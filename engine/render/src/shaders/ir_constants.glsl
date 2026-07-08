// Shared GPU constants for all trixel pipeline shaders.
// Included via #include "ir_constants.glsl" (resolved by the engine's
// shader preprocessor at compile time).

const int VOXEL_CHUNK_SIZE = 256;
const int kInvalidDepth = 0x7FFFFFFF;

// Voxel stage-1/stage-2 micro-slice packing (#2258). Each stage workgroup runs
// this many z-slices (`local_size_z`), so the compact dispatches
// divCeil(zTotal, kStageMicroSlicesPerGroup) workgroups instead of one per
// micro-slice. Byte-identical output (the z-slices are independent atomic
// writers); it only cuts the launched-workgroup count. Must stay in lockstep
// with the `local_size_z` literal in c_voxel_to_trixel_stage_{1,2}.glsl, the
// compact's writeDispatchDims numGroupsZ, and the Metal twins (ir_constants.metal
// + the (2,3,N) threadgroup-size map in metal_pipeline.cpp).
const int kStageMicroSlicesPerGroup = 8;
