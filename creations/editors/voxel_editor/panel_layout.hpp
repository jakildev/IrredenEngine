#ifndef IR_VOXEL_EDITOR_PANEL_LAYOUT_H
#define IR_VOXEL_EDITOR_PANEL_LAYOUT_H

#include <irreden/ir_math.hpp>

#include "array_panel.hpp"
#include "components_panel.hpp"
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

constexpr bool panelColumnsDisjoint(
    IRMath::ivec2 firstPos,
    IRMath::ivec2 firstSize,
    IRMath::ivec2 secondPos,
    IRMath::ivec2 secondSize
) {
    return firstPos.x + firstSize.x <= secondPos.x || secondPos.x + secondSize.x <= firstPos.x;
}

static_assert(
    panelRectsDisjoint(kRecipesPanelPos, kRecipesPanelSize, kBonePanelPos, kBonePanelSize)
);
static_assert(
    panelRectsDisjoint(kRecipesPanelPos, kRecipesPanelSize, kSkeletonPanelPos, kSkeletonPanelSize)
);
static_assert(panelRectsDisjoint(kRecipesPanelPos, kRecipesPanelSize, kLodPanelPos, kLodPanelSize));
static_assert(panelRectsDisjoint(
    kRecipesPanelPos, kRecipesPanelSize, kComponentsPanelPos, kComponentsPanelSize
));
static_assert(
    panelRectsDisjoint(kRecipesPanelPos, kRecipesPanelSize, kArrayPanelPos, kArrayPanelSize)
);

static_assert(
    panelRectsDisjoint(modulePanelPos(0), kModulePanelSize, kBonePanelPos, kBonePanelSize)
);
static_assert(
    panelRectsDisjoint(modulePanelPos(0), kModulePanelSize, kSkeletonPanelPos, kSkeletonPanelSize)
);
static_assert(panelRectsDisjoint(modulePanelPos(0), kModulePanelSize, kLodPanelPos, kLodPanelSize));
static_assert(panelRectsDisjoint(
    modulePanelPos(0), kModulePanelSize, kComponentsPanelPos, kComponentsPanelSize
));
static_assert(
    panelRectsDisjoint(modulePanelPos(0), kModulePanelSize, kArrayPanelPos, kArrayPanelSize)
);
static_assert(
    panelColumnsDisjoint(modulePanelPos(0), kModulePanelSize, kBonePanelPos, kBonePanelSize)
);
static_assert(
    panelColumnsDisjoint(modulePanelPos(0), kModulePanelSize, kSkeletonPanelPos, kSkeletonPanelSize)
);
static_assert(
    panelColumnsDisjoint(modulePanelPos(0), kModulePanelSize, kLodPanelPos, kLodPanelSize)
);
static_assert(panelColumnsDisjoint(
    modulePanelPos(0), kModulePanelSize, kComponentsPanelPos, kComponentsPanelSize
));

static_assert(
    panelRectsDisjoint(kBonePanelPos, kBonePanelSize, kSkeletonPanelPos, kSkeletonPanelSize)
);
static_assert(panelRectsDisjoint(kBonePanelPos, kBonePanelSize, kLodPanelPos, kLodPanelSize));
static_assert(
    panelRectsDisjoint(kBonePanelPos, kBonePanelSize, kComponentsPanelPos, kComponentsPanelSize)
);
static_assert(panelRectsDisjoint(kBonePanelPos, kBonePanelSize, kArrayPanelPos, kArrayPanelSize));
static_assert(
    panelRectsDisjoint(kSkeletonPanelPos, kSkeletonPanelSize, kLodPanelPos, kLodPanelSize)
);
static_assert(panelRectsDisjoint(
    kSkeletonPanelPos, kSkeletonPanelSize, kComponentsPanelPos, kComponentsPanelSize
));
static_assert(
    panelRectsDisjoint(kSkeletonPanelPos, kSkeletonPanelSize, kArrayPanelPos, kArrayPanelSize)
);

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_PANEL_LAYOUT_H */
