// Stage-2 per-axis COLOUR dispatch — Metal twin of
// c_voxel_to_trixel_stage_2_per_axis.glsl. A thin wrapper that specializes the
// shared body at compile time: IR_PER_AXIS_PASS 1 walks the bound axis list,
// one thread per entry, and writes colour + entity id at the face's store cell
// only for the face the per-axis election admitted, with every cardinal and
// detached path textually absent. The kernel name must equal the file stem
// (metalFunctionNameForStage keys off it), and it MUST be registered in
// metal_pipeline.cpp's threadgroupSizeForFunctionName (at
// kPerAxisEntriesPerGroup threads) and functionUsesImageAtomicScratch lists.
#include "ir_iso_common.metal"
#include "ir_constants.metal"
#include "ir_voxel_dispatch.metal"

#define IR_STORE_WINNER_ELECTION 0
#define IR_PER_AXIS_PASS 1
#define IR_STAGE2_KERNEL_NAME c_voxel_to_trixel_stage_2_per_axis
#include "ir_voxel_face_select.metal"
#include "ir_source_face_depth.metal"
#include "c_voxel_to_trixel_stage_2_body.metal"
