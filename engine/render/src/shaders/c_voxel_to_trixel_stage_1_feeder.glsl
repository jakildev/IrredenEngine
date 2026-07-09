/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_1_feeder.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Stage-1 SHADOW-FEEDER dispatch (#2258 Step B, architect option a′). A thin
// wrapper that specializes the shared body at compile time: IR_FEEDER_PASS 1
// compiles the feeder path (read the compacted buffer's TAIL of off-screen
// casters, raster a strided feederSubCap² micro-grid instead of the visible
// effSub²). Dispatched UNCONDITIONALLY as a SECOND stage-1 program in
// system_voxel_to_trixel.hpp, reading indirect struct 1 — it is empty (every
// workgroup early-returns) when the compact classified zero feeders, so no
// `feederSubCap_ > 0` guard is needed (feederSubCap() floors at cappedEffSub and
// never returns 0). The visible twin is c_voxel_to_trixel_stage_1.glsl
// (IR_FEEDER_PASS 0). Includes come BEFORE the body (non-recursive GLSL include
// resolver); see the body file's header for the idiom.
#version 450 core
#define IR_FEEDER_PASS 1
#include "ir_iso_common.glsl"
#include "ir_constants.glsl"
#include "c_voxel_to_trixel_stage_1_body.glsl"
