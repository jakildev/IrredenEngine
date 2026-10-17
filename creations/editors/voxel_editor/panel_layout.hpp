#ifndef IR_VOXEL_EDITOR_PANEL_LAYOUT_H
#define IR_VOXEL_EDITOR_PANEL_LAYOUT_H

#include "array_panel.hpp"
#include "components_panel.hpp"
#include "parts_panel.hpp"
#include "recipes_panel.hpp"

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kBonePanelPos{378, 240};
constexpr IRMath::ivec2 kBonePanelSize{120, 96};
constexpr IRMath::ivec2 kSkeletonPanelPos{378, 342};
constexpr IRMath::ivec2 kSkeletonPanelSize{120, 114};

constexpr bool panelRectsDisjoint(
    IRMath::ivec2 firstPos,
    IRMath::ivec2 firstSize,
    IRMath::ivec2 secondPos,
    IRMath::ivec2 secondSize
) {
    return firstPos.x + firstSize.x <= secondPos.x || secondPos.x + secondSize.x <= firstPos.x ||
           firstPos.y + firstSize.y <= secondPos.y || secondPos.y + secondSize.y <= firstPos.y;
}

#define IR_VOXEL_EDITOR_ASSERT_DISJOINT(first, second)                                             \
    static_assert(                                                                                 \
        panelRectsDisjoint(                                                                        \
            k##first##PanelPos,                                                                    \
            k##first##PanelSize,                                                                   \
            k##second##PanelPos,                                                                   \
            k##second##PanelSize                                                                   \
        ),                                                                                         \
        #first " and " #second " panels must not overlap"                                          \
    )

IR_VOXEL_EDITOR_ASSERT_DISJOINT(Recipes, Bone);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Recipes, Skeleton);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Recipes, Lod);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Recipes, Components);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Recipes, Array);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Recipes, Parts);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Bone, Skeleton);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Bone, Lod);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Bone, Components);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Bone, Array);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Bone, Parts);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Skeleton, Lod);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Skeleton, Components);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Skeleton, Array);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Skeleton, Parts);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Lod, Components);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Lod, Array);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Lod, Parts);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Components, Array);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Components, Parts);
IR_VOXEL_EDITOR_ASSERT_DISJOINT(Array, Parts);

static_assert(
    panelRectsDisjoint(modulePanelPos(0), kModulePanelSize, kBonePanelPos, kBonePanelSize),
    "module and Bone panels must not overlap"
);
static_assert(
    panelRectsDisjoint(modulePanelPos(0), kModulePanelSize, kSkeletonPanelPos, kSkeletonPanelSize),
    "module and Skeleton panels must not overlap"
);
static_assert(
    panelRectsDisjoint(modulePanelPos(0), kModulePanelSize, kLodPanelPos, kLodPanelSize),
    "module and Lod panels must not overlap"
);
static_assert(
    panelRectsDisjoint(
        modulePanelPos(0), kModulePanelSize, kComponentsPanelPos, kComponentsPanelSize
    ),
    "module and Components panels must not overlap"
);
static_assert(
    panelRectsDisjoint(modulePanelPos(0), kModulePanelSize, kArrayPanelPos, kArrayPanelSize),
    "module and Array panels must not overlap"
);
static_assert(
    panelRectsDisjoint(modulePanelPos(0), kModulePanelSize, kPartsPanelPos, kPartsPanelSize),
    "module and Parts panels must not overlap"
);

#undef IR_VOXEL_EDITOR_ASSERT_DISJOINT

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_PANEL_LAYOUT_H */
