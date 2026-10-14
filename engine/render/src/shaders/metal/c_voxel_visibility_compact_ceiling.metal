// Voxel visibility compact with the fog ceiling envelope — Metal twin of
// c_voxel_visibility_compact_ceiling.glsl. IR_FOG_CEILING_PASS 1 compiles the
// shared body with the upper-envelope whole-voxel drop and the lower-envelope
// per-axis neighbour admission; dispatched only for a canvas whose bound fog
// window has a ceiling-enabled source live. IR_COMPACT_KERNEL_NAME must equal
// the file stem.
#include "ir_iso_common.metal"
#include "ir_constants.metal"
#include "ir_voxel_dispatch.metal"

#define IR_FOG_CEILING_PASS 1
#define IR_COMPACT_KERNEL_NAME c_voxel_visibility_compact_ceiling
#include "c_voxel_visibility_compact_body.metal"
