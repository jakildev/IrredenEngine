/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_1.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Stage-1 VISIBLE dispatch. A thin wrapper that specializes the shared body at
// compile time: IR_FEEDER_PASS 0 compiles the body with the feeder tail-read +
// strided micro-grid textually absent — no runtime predication tax on the
// hottest kernel in the engine. The feeder twin is
// c_voxel_to_trixel_stage_1_feeder.glsl (IR_FEEDER_PASS 1). Includes come
// BEFORE the body because the body declares no #includes of its own.
#version 450 core
#define IR_FEEDER_PASS 0
#define IR_STORE_WINNER_ELECTION 0
#include "ir_iso_common.glsl"
#define IR_VOXEL_FOG_GRID_BINDING 0
#include "ir_constants.glsl"
#include "ir_voxel_face_select.glsl"
#include "c_voxel_to_trixel_stage_1_body.glsl"
