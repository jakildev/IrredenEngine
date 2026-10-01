#ifndef COMPONENT_CANVAS_RESIDENCY_SETTINGS_H
#define COMPONENT_CANVAS_RESIDENCY_SETTINGS_H

#include <irreden/ir_constants.hpp>

namespace IRComponents {

// World-scoped canvas-residency policy (singleton). Margins are iso units
// beyond the visible viewport.
struct C_CanvasResidencySettings {
    // Entity canvases allowed alive at once, managed or not.
    int liveCanvasBudget_ = IRConstants::kEntityCanvasLiveBudget;
    int promotionsPerFrame_ = IRConstants::kEntityCanvasPromotionsPerFrame;
    int promoteMarginIso_ = IRConstants::kEntityCanvasPromoteMarginIso;
    // Read as at least the promote margin: a narrower demote region would
    // promote and demote the same entity on alternate frames.
    int demoteMarginIso_ = IRConstants::kEntityCanvasDemoteMarginIso;
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_RESIDENCY_SETTINGS_H */
