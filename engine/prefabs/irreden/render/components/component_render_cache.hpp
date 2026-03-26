#ifndef COMPONENT_RENDER_CACHE_H
#define COMPONENT_RENDER_CACHE_H

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
