// Shared GPU constants for all trixel pipeline shaders.
// Included via #include "ir_constants.glsl" (resolved by the engine's
// shader preprocessor at compile time).

const int VOXEL_CHUNK_SIZE = 256;
const int kInvalidDepth = 0x7FFFFFFF;
