/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_2.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Stage-2 DEFAULT dispatch (#2346, the stage-1 #2258 a′ idiom). A thin wrapper
// that specializes the shared body at compile time: IR_STORE_WINNER_ELECTION 0
// compiles the body with the cardinal winner guard textually absent, so this
// is byte-for-byte master's stage-2 kernel — no runtime predication tax. The
// winner-guarded twin is c_voxel_to_trixel_stage_2_winner.glsl (ELECTION 1).
// Includes come BEFORE the body because GLSL's include resolver is
// non-recursive (the body declares no #includes of its own). See the body
// file's header for the idiom.
#version 450 core
#define IR_STORE_WINNER_ELECTION 0
#include "ir_iso_common.glsl"
#define IR_VOXEL_FOG_GRID_BINDING 3
#include "ir_constants.glsl"
#include "ir_voxel_face_select.glsl"
#include "c_voxel_to_trixel_stage_2_body.glsl"
