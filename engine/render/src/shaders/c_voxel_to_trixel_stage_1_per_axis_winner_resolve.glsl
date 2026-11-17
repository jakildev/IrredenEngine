// Stage-1 per-axis WINNER-ELECTION dispatch. A thin wrapper that specializes
// the shared body at compile time: IR_PER_AXIS_PASS 1 with
// IR_STORE_WINNER_ELECTION 1 re-walks the bound axis list with the store's face
// selection and, for each face whose encoded distance matches the settled cell,
// atomicMins its run-stable voxel pool index into the winner scratch
// (binding 28) that c_voxel_to_trixel_stage_2_per_axis guards on. Dispatched per
// axis between the stores and that axis's colour pass. The store twin is
// c_voxel_to_trixel_stage_1_per_axis.glsl. Includes come BEFORE the body
// because the body declares no #includes of its own.
#version 450 core
#define IR_FEEDER_PASS 0
#define IR_STORE_WINNER_ELECTION 1
#define IR_PER_AXIS_PASS 1
#include "ir_iso_common.glsl"
#define IR_VOXEL_FOG_GRID_BINDING 0
#include "ir_constants.glsl"
#include "ir_voxel_dispatch.glsl"
#include "ir_voxel_face_select.glsl"
#include "c_voxel_to_trixel_stage_1_body.glsl"
