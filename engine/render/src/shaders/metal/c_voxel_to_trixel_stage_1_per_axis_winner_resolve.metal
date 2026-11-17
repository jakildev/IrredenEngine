// Stage-1 per-axis WINNER-ELECTION dispatch — Metal twin of
// c_voxel_to_trixel_stage_1_per_axis_winner_resolve.glsl. A thin wrapper that
// specializes the shared body at compile time: IR_PER_AXIS_PASS 1 with
// IR_STORE_WINNER_ELECTION 1 re-walks the bound axis list with the store's face
// selection and elects each settled cell's minimum voxel pool index into the
// buffer-28 winner scratch that c_voxel_to_trixel_stage_2_per_axis guards on.
// IR_STAGE1_KERNEL_NAME must equal the file stem, and that name MUST be
// registered in metal_pipeline.cpp's threadgroupSizeForFunctionName (at
// kPerAxisEntriesPerGroup threads) and functionUsesImageAtomicScratch lists.
// The store twin is c_voxel_to_trixel_stage_1_per_axis.metal.
#include "ir_iso_common.metal"
#include "ir_constants.metal"
#include "ir_voxel_dispatch.metal"

#define IR_FEEDER_PASS 0
#define IR_STORE_WINNER_ELECTION 1
#define IR_PER_AXIS_PASS 1
#define IR_STAGE1_KERNEL_NAME c_voxel_to_trixel_stage_1_per_axis_winner_resolve
#include "ir_voxel_face_select.metal"
#include "c_voxel_to_trixel_stage_1_body.metal"
