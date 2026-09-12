// Stage-2 DEFAULT dispatch — Metal twin of c_voxel_to_trixel_stage_2.glsl. A
// thin wrapper that specializes the shared body at compile time:
// IR_STORE_WINNER_ELECTION 0 compiles the body with the cardinal winner guard
// textually absent, so it costs no runtime predication. The kernel name must
// equal the file stem (metalFunctionNameForStage keys off it). The
// winner-guarded variant is c_voxel_to_trixel_stage_2_winner.metal.
#include "ir_iso_common.metal"
#include "ir_constants.metal"

#define IR_STORE_WINNER_ELECTION 0
#define IR_STAGE2_KERNEL_NAME c_voxel_to_trixel_stage_2
#include "ir_voxel_face_select.metal"
#include "c_voxel_to_trixel_stage_2_body.metal"
