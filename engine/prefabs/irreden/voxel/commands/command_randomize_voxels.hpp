#ifndef COMMAND_RANDOMIZE_VOXELS_H
#define COMMAND_RANDOMIZE_VOXELS_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_locked.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

namespace IRCommand {

// Randomize the RGB of every active voxel across all voxel sets, skipping any
// entity tagged `C_Locked` (#17). Implemented as a one-shot query — the run-now
// counterpart to a system tick — which is exactly what the enum's original note
// asked for ("should operate like a system with a query ... exclude locked
// entities"). The per-voxel alpha is preserved and alpha-0 (carved / inactive)
// voxels are skipped, so `editVoxels`' resync leaves the pool's active-mask and
// face occupancy unchanged — only visible colors move.
template <> struct Command<RANDOMIZE_VOXELS> {
    static auto create() {
        return []() {
            IRSystem::executeQuery<
                IRComponents::C_VoxelSetNew,
                IRSystem::Exclude<IRComponents::C_Locked>>([](IRComponents::C_VoxelSetNew &set) {
                set.editVoxels([](int, IRComponents::C_Voxel &voxel, IRMath::vec3) {
                    if (voxel.color_.alpha_ == 0) {
                        return; // carved / inactive — keep it (mask stays stable)
                    }
                    IRMath::Color randomized = IRMath::randomColor();
                    randomized.alpha_ = voxel.color_.alpha_; // preserve original alpha
                    voxel.color_ = randomized;
                });
            });
        };
    }
};

} // namespace IRCommand

#endif /* COMMAND_RANDOMIZE_VOXELS_H */
