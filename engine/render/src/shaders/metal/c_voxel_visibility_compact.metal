// Voxel visibility compact, no fog ceiling — Metal twin of
// c_voxel_visibility_compact.glsl. IR_FOG_CEILING_PASS 0 compiles the shared
// body with the ceiling envelope and the per-axis neighbour admission textually
// absent. IR_COMPACT_KERNEL_NAME must equal the file stem — the pipeline
// derives the function name from it. The ceiling twin is
// c_voxel_visibility_compact_ceiling.metal.
#include "ir_iso_common.metal"
#include "ir_constants.metal"
#include "ir_voxel_dispatch.metal"

#define IR_FOG_CEILING_PASS 0
#define IR_COMPACT_KERNEL_NAME c_voxel_visibility_compact
#include "c_voxel_visibility_compact_body.metal"
