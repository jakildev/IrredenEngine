#ifndef IR_RENDER_VOXEL_DISPATCH_GRID_H
#define IR_RENDER_VOXEL_DISPATCH_GRID_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

namespace IRSystem {

// Folds a 1D compute-dispatch count into a 2D workgroup grid, capping the X
// dimension at 1024 so anything larger spills into additional rows. Each
// consuming compute shader reconstructs the linear work item index from the
// 2D dispatch (e.g. `workGroupIndex * localSize + localId.x`), so the X cap
// here is load-bearing: it must match what the shaders unfold. That coupling
// is exactly why this is one shared helper rather than re-inlined per call
// site — every voxel/shape dispatch must fold a given count identically.
//
// `count` is the number of work items to spread across the grid; for a
// workgroup grid, callers divide their thread count by the shader's local
// size first (e.g. `divCeil(liveCount, 64)`). Callers must guarantee
// `count > 0`; the assert below enforces it. `count == 0` divides by zero
// here — a SIGFPE on x86 Linux, but silently yields a (0,0) grid on Apple
// Silicon (#1619), so the contract is asserted at entry rather than left to
// each caller's discipline. The one legitimate empty-pool path
// (`buildVoxelFrameData`, lighting on an empty canvas) clamps to 1 before
// calling — see voxel_frame_data.hpp.
inline IRMath::ivec2 voxelDispatchGridForCount(int count) {
    IR_ASSERT(
        count > 0,
        "voxelDispatchGridForCount: count must be > 0 — division by zero "
        "is UB (silent on Apple Silicon)"
    );
    constexpr int kMaxDispatchGroupsX = 1024;
    const int groupsX = IRMath::min(count, kMaxDispatchGroupsX);
    const int groupsY = IRMath::divCeil(count, groupsX);
    return IRMath::ivec2(groupsX, groupsY);
}

} // namespace IRSystem

#endif // IR_RENDER_VOXEL_DISPATCH_GRID_H
