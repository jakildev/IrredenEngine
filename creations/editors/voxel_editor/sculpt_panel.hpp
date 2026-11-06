#ifndef IR_VOXEL_EDITOR_SCULPT_PANEL_H
#define IR_VOXEL_EDITOR_SCULPT_PANEL_H

#include <irreden/ir_math.hpp>

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kSculptToolbarPos{4, 60};
constexpr IRMath::ivec2 kSculptButtonSize{66, 18};
constexpr int kSculptButtonStride = 70;
constexpr IRMath::ivec2 kSculptToolbarSize{9 * kSculptButtonStride, 18};

inline IRMath::vec2 sculptButtonCenter(int index) {
    return IRMath::vec2(kSculptToolbarPos + IRMath::ivec2(index * kSculptButtonStride, 0)) +
           IRMath::vec2(kSculptButtonSize) * 0.5f;
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_SCULPT_PANEL_H */
