/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_2.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Stage-2 DEFAULT dispatch. A thin wrapper that specializes the shared body at
// compile time: IR_STORE_WINNER_ELECTION 0 compiles the body with the cardinal
// winner guard textually absent, so it costs no runtime predication. The
// winner-guarded variant is c_voxel_to_trixel_stage_2_winner.glsl (ELECTION 1).
// Includes come BEFORE the body because the body declares no #includes of its
// own.
#version 450 core
#define IR_STORE_WINNER_ELECTION 0
#include "ir_iso_common.glsl"
#define IR_VOXEL_FOG_GRID_BINDING 3
#include "ir_constants.glsl"
#include "ir_voxel_face_select.glsl"
#include "c_voxel_to_trixel_stage_2_body.glsl"
