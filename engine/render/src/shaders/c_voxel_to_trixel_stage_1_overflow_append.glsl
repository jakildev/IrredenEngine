// Stage-1 per-axis OVERFLOW APPEND dispatch. A thin wrapper that specializes
// the shared body at compile time: IR_OVERFLOW_APPEND 1 compiles the per-axis
// route as the append, with the occluder-pyramid test, and leaves the store
// and election taps textually absent. The store twin is
// c_voxel_to_trixel_stage_1.glsl. Includes come BEFORE the body because the
// body declares no #includes of its own.
#version 450 core
#define IR_FEEDER_PASS 0
#define IR_STORE_WINNER_ELECTION 0
#define IR_OVERFLOW_APPEND 1
#include "ir_iso_common.glsl"
#define IR_VOXEL_FOG_GRID_BINDING 0
#include "ir_constants.glsl"
#include "ir_voxel_dispatch.glsl"
#include "ir_voxel_face_select.glsl"
#include "ir_per_axis_face_footprint.glsl"
layout(binding = 2) uniform isampler2D occluderDepth;
#include "ir_per_axis_occlusion.glsl"
#include "c_voxel_to_trixel_stage_1_body.glsl"
