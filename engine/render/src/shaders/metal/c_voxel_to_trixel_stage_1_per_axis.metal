// Stage-1 per-axis STORE dispatch — Metal twin of
// c_voxel_to_trixel_stage_1_per_axis.glsl. A thin wrapper that specializes the
// shared body at compile time: IR_PER_AXIS_PASS 1 compiles the per-axis list
// walk, one thread per entry of the bound axis list, as the atomic-min distance
// store, with every cardinal and detached path textually absent.
// IR_STAGE1_KERNEL_NAME must equal the file stem — metalFunctionNameForStage
// derives the function name from it — and that name MUST be registered in
// metal_pipeline.cpp's threadgroupSizeForFunctionName (at
// kPerAxisEntriesPerGroup threads) and functionUsesImageAtomicScratch lists.
// The election twin is c_voxel_to_trixel_stage_1_per_axis_winner_resolve.metal.
#include "ir_iso_common.metal"
#include "ir_constants.metal"
#include "ir_voxel_dispatch.metal"

#define IR_FEEDER_PASS 0
#define IR_STORE_WINNER_ELECTION 0
#define IR_PER_AXIS_PASS 1
#define IR_STAGE1_KERNEL_NAME c_voxel_to_trixel_stage_1_per_axis
#include "ir_voxel_face_select.metal"
#include "c_voxel_to_trixel_stage_1_body.metal"
