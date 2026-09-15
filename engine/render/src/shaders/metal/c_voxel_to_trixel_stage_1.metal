// Stage-1 VISIBLE dispatch — Metal twin of c_voxel_to_trixel_stage_1.glsl. A
// thin wrapper that specializes the shared body at compile time: IR_FEEDER_PASS 0
// compiles the body with the feeder tail-read + strided micro-grid textually
// absent (no runtime predication tax). IR_STAGE1_KERNEL_NAME must equal the file
// stem — metalFunctionNameForStage derives the function name from it. The feeder
// twin is c_voxel_to_trixel_stage_1_feeder.metal.
#include "ir_iso_common.metal"
#include "ir_constants.metal"

#define IR_FEEDER_PASS 0
#define IR_STORE_WINNER_ELECTION 0
#define IR_STAGE1_KERNEL_NAME c_voxel_to_trixel_stage_1
#include "ir_voxel_face_select.metal"
#include "c_voxel_to_trixel_stage_1_body.metal"
