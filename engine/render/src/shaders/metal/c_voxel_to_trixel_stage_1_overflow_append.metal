// Stage-1 per-axis OVERFLOW APPEND dispatch — Metal twin of
// c_voxel_to_trixel_stage_1_overflow_append.glsl. A thin wrapper that
// specializes the shared body at compile time: IR_OVERFLOW_APPEND 1 compiles
// the per-axis route as the append, with the occluder-pyramid test, and leaves
// the store and election taps textually absent. IR_STAGE1_KERNEL_NAME must
// equal the file stem — metalFunctionNameForStage derives the function name
// from it. The store twin is c_voxel_to_trixel_stage_1.metal.
#include "ir_iso_common.metal"
#include "ir_constants.metal"
#include "ir_voxel_dispatch.metal"

#define IR_FEEDER_PASS 0
#define IR_STORE_WINNER_ELECTION 0
#define IR_OVERFLOW_APPEND 1
#define IR_STAGE1_KERNEL_NAME c_voxel_to_trixel_stage_1_overflow_append
#include "ir_voxel_face_select.metal"
#include "ir_per_axis_occlusion.metal"
#include "c_voxel_to_trixel_stage_1_body.metal"
