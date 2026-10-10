#ifndef COMPONENT_FOG_GHOST_H
#define COMPONENT_FOG_GHOST_H

#include <irreden/common/components/component_world_transform.hpp>

namespace IRComponents {

struct C_FogGhost {
    C_WorldTransform pose_{};
    bool valid_ = false;
};

} // namespace IRComponents

#endif /* COMPONENT_FOG_GHOST_H */
