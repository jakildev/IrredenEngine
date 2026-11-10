#ifndef IR_VOXEL_EDITOR_PART_SIZE_PANEL_H
#define IR_VOXEL_EDITOR_PART_SIZE_PANEL_H

#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/ir_math.hpp>

#include "integer_stepper.hpp"

namespace IRVoxelEditor {
constexpr IRMath::ivec2 kPartSizePanelPos{4, 660};
constexpr IRMath::ivec2 kPartSizePanelSize{204, 134};
constexpr int kPartSizeAxisCount = 3;
constexpr IRMath::ivec2 partSizeRowPos(int axis) {
    return kPartSizePanelPos + IRMath::ivec2(98, 23 + axis * 22);
}
constexpr IRMath::ivec2 partSizeFieldPos(int axis) {
    return integerFieldPos(partSizeRowPos(axis));
}
constexpr IRMath::ivec2 partSizeStepPos(int axis, int direction) {
    return integerStepPos(partSizeRowPos(axis), direction);
}
constexpr IRMath::ivec2 kPartSizeCreatePos{kPartSizePanelPos + IRMath::ivec2(8, 92)};
constexpr IRMath::ivec2 kPartSizeCreateSize{184, 18};
constexpr IRMath::ivec2 kPartSizeReadoutPos{kPartSizePanelPos + IRMath::ivec2(8, 116)};

constexpr int partSizeMax(int sceneAxisSize) {
    return IRMath::max(64, sceneAxisSize);
}

// World z of a voxel set's far (local z == size.z - 1) layer: the seed ground
// plane of the single-set scene, and the layer every created part rests on.
constexpr int kSeedGroundPlaneWorldZ = 3;

// Translation that centres a set of @p size in X / Y and seats its far z layer
// at kSeedGroundPlaneWorldZ, so the camera framing and the authoring recipes
// stay height-agnostic. An odd X / Y extent gives a half-integer origin.
inline IRMath::vec3 deriveSceneOrigin(IRMath::ivec3 size) {
    return IRMath::vec3(
        -static_cast<float>(size.x) / 2.0f,
        -static_cast<float>(size.y) / 2.0f,
        static_cast<float>(kSeedGroundPlaneWorldZ - (size.z - 1))
    );
}

// Translation of a CENTER-anchored voxel part of @p size whose cell (0,0,0)
// sits at deriveSceneOrigin(size): the middle of its box, which is the point
// the part turns about.
inline IRMath::vec3 derivePartPivot(IRMath::ivec3 size) {
    return deriveSceneOrigin(size) -
           IRComponents::anchorOffset(IRComponents::EntityAnchor::CENTER, size);
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_PART_SIZE_PANEL_H */
