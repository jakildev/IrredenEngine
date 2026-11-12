#ifndef IR_PREFAB_OCCLUSION_LAG_SOURCE_H
#define IR_PREFAB_OCCLUSION_LAG_SOURCE_H

// Frame-to-frame contract between COMPUTE_DISTANCE_HIZ and the voxel occlusion
// cull (docs/design/voxel-occlusion-culling.md): the cull in frame N tests the
// Hi-Z chain frame N-1 built, and the chain has no other reader. Pure policy,
// so it runs headlessly.

#include <irreden/ir_math.hpp>

namespace IRPrefab::OcclusionCull {

// COMPUTE_DISTANCE_HIZ builds the chain only on a frame whose successor can
// cull from it: a cull-off frame leaves the chain from before the gap in place.
constexpr bool buildsDistanceHiZ(bool cullEnabled) {
    return cullEnabled;
}

// What a frame's view says about the chain the previous frame built.
struct LagSourceView {
    IRMath::vec2 cameraIso_{0.0f};
    IRMath::vec2 zoom_{0.0f};
    int density_ = 0;
    // Camera iso travel above which last frame's view is unrelated to this one.
    float cutThreshold_ = 0.0f;
    bool backingResized_ = false;
};

// Decides once per frame whether the chain the cull would read is stale: built
// for a different view (a camera cut, a zoom or density change, a backing
// resize), or not built at all because the previous frame had the cull off.
// Staleness disables the cull for that frame, which keeps every chunk.
class LagSource {
  public:
    // Returns whether the chain is stale this frame. A cull-off frame builds
    // no chain, so the first frame after a re-enable is stale.
    bool advance(bool cullEnabled, const LagSourceView &view) {
        if (!cullEnabled) {
            m_hasLast = false;
            return true;
        }
        const bool stale = view.backingResized_ || !m_hasLast || m_last.density_ != view.density_ ||
                           m_last.zoom_ != view.zoom_ ||
                           IRMath::length(view.cameraIso_ - m_last.cameraIso_) > view.cutThreshold_;
        m_last = view;
        m_hasLast = true;
        return stale;
    }

  private:
    LagSourceView m_last{};
    bool m_hasLast = false;
};

} // namespace IRPrefab::OcclusionCull

#endif /* IR_PREFAB_OCCLUSION_LAG_SOURCE_H */
