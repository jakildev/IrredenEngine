// Stage-1 per-axis STORE dispatch. A thin wrapper that specializes the shared
// body at compile time: IR_PER_AXIS_PASS 1 compiles the per-axis list walk, one
// invocation per entry of the bound axis list, as the atomic-min distance store,
// with every cardinal and detached path textually absent. The election twin is
// c_voxel_to_trixel_stage_1_per_axis_winner_resolve.glsl. Includes come BEFORE
// the body because the body declares no #includes of its own.
#version 450 core
#define IR_FEEDER_PASS 0
#define IR_STORE_WINNER_ELECTION 0
#define IR_PER_AXIS_PASS 1
#include "ir_iso_common.glsl"
#define IR_VOXEL_FOG_GRID_BINDING 0
#include "ir_constants.glsl"
#include "ir_voxel_dispatch.glsl"
#include "ir_voxel_face_select.glsl"
#include "c_voxel_to_trixel_stage_1_body.glsl"
