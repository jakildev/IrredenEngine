#ifndef COMPONENT_COLLISION_LAYER_H
#define COMPONENT_COLLISION_LAYER_H

#include <cstdint>

namespace IRComponents {

enum CollisionLayerMask : std::uint32_t {
    COLLISION_LAYER_DEFAULT = 1u << 0u,
    COLLISION_LAYER_NOTE_BLOCK = 1u << 1u,
    COLLISION_LAYER_NOTE_PLATFORM = 1u << 2u,
    COLLISION_LAYER_PARTICLE = 1u << 3u,
};

struct C_CollisionLayer {
    std::uint32_t layer_;
    std::uint32_t collidesWithMask_;
    bool isSolid_;

    C_CollisionLayer(std::uint32_t layer, std::uint32_t collidesWithMask, bool isSolid = true)
        : layer_{layer}, collidesWithMask_{collidesWithMask}, isSolid_{isSolid} {}

    C_CollisionLayer()
        : C_CollisionLayer(COLLISION_LAYER_DEFAULT, COLLISION_LAYER_DEFAULT, true) {}

    bool canCollideWith(const C_CollisionLayer &other) const {
        if (!isSolid_ || !other.isSolid_) {
            return false;
        }
        return (collidesWithMask_ & other.layer_) != 0u;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_COLLISION_LAYER_H */
