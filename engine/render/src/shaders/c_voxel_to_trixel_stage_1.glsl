/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_1.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Stage-1 VISIBLE dispatch (#2258 Step B, architect option a′). A thin wrapper
// that specializes the shared body at compile time: IR_FEEDER_PASS 0 compiles
// the body with the feeder tail-read + strided micro-grid textually absent, so
// this is byte-for-byte master's stage-1 kernel — no runtime `feederPass`
// predication tax on the hottest kernel in the engine. The feeder twin is
// c_voxel_to_trixel_stage_1_feeder.glsl (IR_FEEDER_PASS 1). Includes come
// BEFORE the body because GLSL's include resolver is non-recursive (the body
// declares no #includes of its own). See the body file's header for the idiom.
#version 450 core
#define IR_FEEDER_PASS 0
#include "ir_iso_common.glsl"
#include "ir_constants.glsl"
#include "c_voxel_to_trixel_stage_1_body.glsl"
