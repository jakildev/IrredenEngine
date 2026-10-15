// Voxel visibility compact, no fog ceiling. A thin wrapper that specializes the
// shared body at compile time: IR_FOG_CEILING_PASS 0 compiles the body with the
// ceiling envelope and the per-axis neighbour admission textually absent, so
// every canvas without a ceiling-enabled source keeps master's kernel. The
// ceiling twin is c_voxel_visibility_compact_ceiling.glsl. Includes come
// BEFORE the body because the body declares no #includes of its own.
#version 450 core
#define IR_FOG_CEILING_PASS 0
#include "ir_iso_common.glsl"
#include "ir_constants.glsl"
#include "ir_voxel_dispatch.glsl"
#include "c_voxel_visibility_compact_body.glsl"
