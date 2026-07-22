// Stage-2 CARDINAL WINNER-GUARDED dispatch (#2346) — Metal twin of
// c_voxel_to_trixel_stage_2_winner.glsl. A thin wrapper that specializes the
// shared body at compile time: IR_STORE_WINNER_ELECTION 1 threads the
// run-stable voxel pool index through emitDeformedFace and guards every
// cardinal colour/entity-id tap with `perAxisWinnerIds[cell] == voxelIndex`
// (the winners elected by c_voxel_to_trixel_stage_1_winner_resolve between the
// stages) — the single-canvas twin of #2255's writeColorTapPerAxis guard.
// Dispatched in place of the default stage 2 ONLY when the ticking pool's
// storeTiesPossible_ flag is set; lattice scenes keep the default kernel. The
// kernel name resolves to c_voxel_to_trixel_stage_2_winner
// (metalFunctionNameForStage keys off the file stem) — it MUST be registered
// in metal_pipeline.cpp's threadgroupSizeForFunctionName (2,3,8) and
// functionUsesImageAtomicScratch lists alongside the default stage 2.
#include "ir_iso_common.metal"
#include "ir_constants.metal"

#define IR_STORE_WINNER_ELECTION 1
#define IR_STAGE2_KERNEL_NAME c_voxel_to_trixel_stage_2_winner
#include "ir_voxel_face_select.metal"
#include "c_voxel_to_trixel_stage_2_body.metal"
