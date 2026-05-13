#ifndef COMPONENT_LAYOUT_STATE_H
#define COMPONENT_LAYOUT_STATE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/layout_types.hpp>

#include <vector>

namespace IRComponents {

struct C_LayoutState {
    std::vector<IRPrefab::Layout::LayoutNode> nodes_;
    int root_ = -1;
    IRMath::ivec2 rootPos_ = {};
    IRMath::ivec2 rootSize_ = {};

    // Splitter drag
    int dragSplitterParent_ = -1;
    int dragSplitterChildIdx_ = -1;
    IRMath::ivec2 dragStartMouse_ = {};
    int dragStartSizeA_ = 0;
    int dragStartSizeB_ = 0;

    // Panel drag-to-dock
    IREntity::EntityId draggedPanelEntity_ = IREntity::kNullEntity;
    int draggedPanelNodeIdx_ = -1;
    IRMath::ivec2 dragOffset_ = {};
    IRMath::ivec2 dragCurrentPos_ = {};
};

} // namespace IRComponents

#endif /* COMPONENT_LAYOUT_STATE_H */
