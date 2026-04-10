#ifndef COMPONENT_RENDER_CACHE_H
#define COMPONENT_RENDER_CACHE_H

// PURPOSE: Generation-based dirty tracking to skip re-rendering static
//   entities. When isStatic_ is true, needsRedraw() returns false unless
//   markDirty() has been called since the last markRendered().
// STATUS: WIP stub -- defined but not used by any system or entity.
// TODO:
//   - Integrate into VOXEL_TO_TRIXEL and/or SHAPES_TO_TRIXEL so static
//     entities skip GPU upload and dispatch when nothing has changed.
//   - Determine which mutations should trigger markDirty() (position
//     change, color change, voxel pool modification, etc.).
// DEPENDENCIES: None (standalone data struct).

#include <cstdint>

namespace IRComponents {

struct C_RenderCache {
    std::uint64_t generation_ = 0;
    std::uint64_t lastRenderedGeneration_ = 0;
    bool isStatic_ = false;

    C_RenderCache() = default;
    C_RenderCache(bool isStatic) : isStatic_{isStatic} {}

    void markDirty() {
        ++generation_;
    }

    bool needsRedraw() const {
        if (!isStatic_) return true;
        return generation_ != lastRenderedGeneration_;
    }

    void markRendered() {
        lastRenderedGeneration_ = generation_;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_RENDER_CACHE_H */
