// Stage-1 SHADOW-FEEDER dispatch — Metal twin of
// c_voxel_to_trixel_stage_1_feeder.glsl. A thin wrapper that specializes the
// shared body at compile time: IR_FEEDER_PASS 1 compiles the feeder path (read
// the compacted buffer's TAIL of off-screen casters, raster a strided
// feederSubCap² micro-grid). IR_STAGE1_KERNEL_NAME must equal the file stem
// (metalFunctionNameForStage derives the function name from it), and that name
// MUST be registered in metal_pipeline.cpp's threadgroupSizeForFunctionName
// (2,3,8) and functionUsesImageAtomicScratch lists. Omission from either fails
// CI naming the kernel — the former via
// cmake/run_metal_kernel_registry_check.cmake, the latter via
// cmake/run_metal_scratch_consumer_check.cmake; a missing scratch entry would
// silently drop the second dispatch's atomic distance writes. The visible twin
// is c_voxel_to_trixel_stage_1.metal.
#include "ir_iso_common.metal"
#include "ir_constants.metal"

#define IR_FEEDER_PASS 1
#define IR_STORE_WINNER_ELECTION 0
#define IR_STAGE1_KERNEL_NAME c_voxel_to_trixel_stage_1_feeder
#include "ir_voxel_face_select.metal"
#include "c_voxel_to_trixel_stage_1_body.metal"
