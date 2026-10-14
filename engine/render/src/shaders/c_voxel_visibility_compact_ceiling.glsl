// Voxel visibility compact with the fog ceiling envelope: IR_FOG_CEILING_PASS 1
// compiles the shared body with the upper-envelope whole-voxel drop and the
// lower-envelope per-axis neighbour admission. VOXEL_TO_TRIXEL_STAGE_1
// dispatches it only for a canvas whose bound fog window has a ceiling-enabled
// source live; the no-ceiling twin is c_voxel_visibility_compact.glsl.
#version 450 core
#define IR_FOG_CEILING_PASS 1
#include "ir_iso_common.glsl"
#include "ir_constants.glsl"
#include "ir_voxel_dispatch.glsl"
#include "c_voxel_visibility_compact_body.glsl"
