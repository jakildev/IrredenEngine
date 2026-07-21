// Stage-2 DEFAULT dispatch (#2346, the stage-1 #2258 a′ idiom) — Metal twin of
// c_voxel_to_trixel_stage_2.glsl. A thin wrapper that specializes the shared
// body at compile time: IR_STORE_WINNER_ELECTION 0 compiles the body with the
// cardinal winner guard textually absent, so this is byte-for-byte master's
// stage-2 kernel — no runtime predication tax. The kernel name resolves to
// c_voxel_to_trixel_stage_2 (metalFunctionNameForStage keys off the file
// stem). The winner-guarded twin is c_voxel_to_trixel_stage_2_winner.metal.
#include "ir_iso_common.metal"
#include "ir_constants.metal"

#define IR_STORE_WINNER_ELECTION 0
#define IR_STAGE2_KERNEL_NAME c_voxel_to_trixel_stage_2
#include "c_voxel_to_trixel_stage_2_body.metal"
