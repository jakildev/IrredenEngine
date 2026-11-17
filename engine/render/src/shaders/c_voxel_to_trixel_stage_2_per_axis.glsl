// Stage-2 per-axis COLOUR dispatch. A thin wrapper that specializes the shared
// body at compile time: IR_PER_AXIS_PASS 1 walks the bound axis list, one
// invocation per entry, and writes colour + entity id at the face's store cell
// only for the face the per-axis election admitted, with every cardinal and
// detached path textually absent. Includes come BEFORE the body (the body
// declares no #includes of its own).
#version 450 core
#define IR_STORE_WINNER_ELECTION 0
#define IR_PER_AXIS_PASS 1
#include "ir_iso_common.glsl"
#define IR_VOXEL_FOG_GRID_BINDING 3
#include "ir_constants.glsl"
#include "ir_voxel_dispatch.glsl"
#include "ir_voxel_face_select.glsl"
#include "ir_source_face_depth.glsl"
#include "c_voxel_to_trixel_stage_2_body.glsl"
