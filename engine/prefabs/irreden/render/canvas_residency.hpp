#ifndef IR_PREFAB_CANVAS_RESIDENCY_H
#define IR_PREFAB_CANVAS_RESIDENCY_H

// Canvas-residency policy: which entities hold an entity canvas this frame.
// An entity canvas is a budgeted resource — the composite draws a bounded
// number of them — so a world of many detached entities keeps canvases only
// for the ones near the camera and lets the rest render through GRID. The
// decision functions here are pure; CANVAS_RESIDENCY applies them.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_canvas_residency_settings.hpp>

#include <algorithm>
#include <vector>

namespace IRPrefab::CanvasResidency {

enum class Verdict : int {
    KEEP = 0,
    PROMOTE,
    DEMOTE,
};

/// One entity's verdict against the interest region. `promoteRegion` lies
/// inside `demoteRegion`; between them an entity keeps whatever it has, which
/// is the hysteresis band.
inline Verdict classify(
    bool resident,
    IRMath::vec2 entityIso,
    const IRMath::IsoBounds2D &promoteRegion,
    const IRMath::IsoBounds2D &demoteRegion
) {
    if (resident) {
        return demoteRegion.contains(entityIso) ? Verdict::KEEP : Verdict::DEMOTE;
    }
    return promoteRegion.contains(entityIso) ? Verdict::PROMOTE : Verdict::KEEP;
}

struct PromotionCandidate {
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    // Squared iso distance from the view centre: nearer entities win a
    // contested budget.
    float distanceSquared_ = 0.0f;
};

/// How many of `candidateCount` promotions may run this frame: bounded by the
/// per-frame promotion budget and by the canvases the live budget still has
/// room for once this frame's demotions have released theirs.
inline int promotionQuota(
    const IRComponents::C_CanvasResidencySettings &settings,
    int liveCanvases,
    int demotions,
    int candidateCount
) {
    const int room = settings.liveCanvasBudget_ - (liveCanvases - demotions);
    return IRMath::max(
        0,
        IRMath::min(IRMath::min(room, settings.promotionsPerFrame_), candidateCount)
    );
}

/// Order the first `quota` candidates nearest-first; entity id breaks ties so
/// the choice is deterministic.
inline void selectNearest(std::vector<PromotionCandidate> &candidates, int quota) {
    const auto nearer = [](const PromotionCandidate &a, const PromotionCandidate &b) {
        if (a.distanceSquared_ != b.distanceSquared_) {
            return a.distanceSquared_ < b.distanceSquared_;
        }
        return a.entity_ < b.entity_;
    };
    std::partial_sort(candidates.begin(), candidates.begin() + quota, candidates.end(), nearer);
}

/// The world's residency settings, created with defaults on first use. Call
/// during setup; a system caches the reference in `beginTick`.
inline IRComponents::C_CanvasResidencySettings &settings() {
    return IREntity::singleton<IRComponents::C_CanvasResidencySettings>();
}

} // namespace IRPrefab::CanvasResidency

#endif /* IR_PREFAB_CANVAS_RESIDENCY_H */
