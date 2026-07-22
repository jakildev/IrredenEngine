// Stage-1 CARDINAL WINNER-ELECTION dispatch (#2346) — Metal twin of
// c_voxel_to_trixel_stage_1_winner_resolve.glsl. A thin wrapper that
// specializes the shared body at compile time: IR_STORE_WINNER_ELECTION 1
// swaps every cardinal-branch distance tap for a resolveWinnerTap, so this
// kernel re-runs the identical single-canvas geometry and atomic-mins each
// tying face's run-stable voxel pool index into the buffer-28 winner scratch
// (the #2255 per-axis election extended to the single-canvas store). Dispatched
// over indirect struct 0 ONLY, between the stage-1 stores and stage 2, when the
// ticking pool's storeTiesPossible_ flag is set. The kernel name resolves to
// c_voxel_to_trixel_stage_1_winner_resolve (metalFunctionNameForStage keys off
// the file stem) — it MUST be registered in metal_pipeline.cpp's
// threadgroupSizeForFunctionName (2,3,8) and functionUsesImageAtomicScratch
// lists, or the dispatch launches wrong-shaped threadgroups / reads a dangling
// distance scratch. The visible twin is c_voxel_to_trixel_stage_1.metal.
#include "ir_iso_common.metal"
#include "ir_constants.metal"

#define IR_FEEDER_PASS 0
#define IR_STORE_WINNER_ELECTION 1
#define IR_STAGE1_KERNEL_NAME c_voxel_to_trixel_stage_1_winner_resolve
#include "ir_voxel_face_select.metal"
#include "c_voxel_to_trixel_stage_1_body.metal"
