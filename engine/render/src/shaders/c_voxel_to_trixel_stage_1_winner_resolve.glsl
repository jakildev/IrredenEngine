/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_1_winner_resolve.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: July 2026
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Stage-1 CARDINAL WINNER-ELECTION dispatch (#2346). A thin wrapper that
// specializes the shared body at compile time: IR_STORE_WINNER_ELECTION 1
// swaps every cardinal-branch distance tap for a resolveWinnerTap, so this
// kernel re-runs the identical single-canvas geometry and, for each face whose
// encoded distance matches the settled per-cell atomicMin winner, atomicMins
// the face's run-stable voxel pool index into the per-cell winner scratch
// (binding 28). Stage 2's winner variant then admits exactly one of the
// equal-key faces — the extension of #2255's per-axis election to the
// single-canvas store, whose (iso pixel, depth) key stops being a bijection of
// live voxels once independently displaced voxels round into the same cell.
// Dispatched over indirect struct 0 ONLY (feeder-won pixels are never
// colour-tapped — #1740's margin), between the stage-1 stores and stage 2,
// and only when the ticking pool's storeTiesPossible_ flag is set — lattice
// scenes never run it. The visible twin is c_voxel_to_trixel_stage_1.glsl.
// Includes come BEFORE the body (non-recursive GLSL include resolver); see the
// body file's header for the idiom.
#version 450 core
#define IR_FEEDER_PASS 0
#define IR_STORE_WINNER_ELECTION 1
#include "ir_iso_common.glsl"
#include "ir_constants.glsl"
#include "c_voxel_to_trixel_stage_1_body.glsl"
