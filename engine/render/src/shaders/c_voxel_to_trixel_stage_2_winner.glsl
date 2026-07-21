/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_2_winner.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: July 2026
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Stage-2 CARDINAL WINNER-GUARDED dispatch (#2346). A thin wrapper that
// specializes the shared body at compile time: IR_STORE_WINNER_ELECTION 1
// threads the run-stable voxel pool index through emitDeformedFace and guards
// every cardinal colour/entity-id tap with `perAxisWinnerIds[cell] ==
// voxelIndex` (the winners elected by
// c_voxel_to_trixel_stage_1_winner_resolve between the stages), so exactly one
// of the faces tying a cell's settled distance key writes — the single-canvas
// twin of #2255's writeColorTapPerAxis guard. Dispatched in place of the
// default stage 2 ONLY when the ticking pool's storeTiesPossible_ flag is set;
// lattice scenes keep the default kernel. Includes come BEFORE the body
// (non-recursive GLSL include resolver); see the body file's header.
#version 450 core
#define IR_STORE_WINNER_ELECTION 1
#include "ir_iso_common.glsl"
#include "ir_constants.glsl"
#include "c_voxel_to_trixel_stage_2_body.glsl"
