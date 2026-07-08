// Shared GPU constants for all trixel pipeline Metal shaders.
// Mirrors shaders/ir_constants.glsl.

constant int VOXEL_CHUNK_SIZE = 256;
constant int kInvalidDepth = 0x7FFFFFFF;

// Voxel stage-1/stage-2 micro-slice packing (#2258). Mirror of
// shaders/ir_constants.glsl kStageMicroSlicesPerGroup — each stage threadgroup
// runs this many z-slices, so the compact dispatches
// divCeil(zTotal, kStageMicroSlicesPerGroup) threadgroups. Must stay in lockstep
// with the (2,3,N) threadgroup-size map entry in metal_pipeline.cpp and the GLSL
// side's local_size_z.
constant int kStageMicroSlicesPerGroup = 8;
