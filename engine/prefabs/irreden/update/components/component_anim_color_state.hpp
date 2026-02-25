#ifndef COMPONENT_ANIM_COLOR_STATE_H
#define COMPONENT_ANIM_COLOR_STATE_H

#include <irreden/ir_math.hpp>
#include <irreden/entity/ir_entity_types.hpp>

using namespace IRMath;

namespace IRComponents {

enum AnimColorBlendMode {
    ANIM_COLOR_BLEND_REPLACE = 0,
    ANIM_COLOR_BLEND_MULTIPLY,
    ANIM_COLOR_BLEND_LERP,
};

struct C_AnimColorState {
    Color baseColor_;
    bool baseInitialized_ = false;
    Color currentColor_;
    AnimColorBlendMode blendMode_;
    IREntity::EntityId lastClipEntity_ = IREntity::kNullEntity;

    C_AnimColorState(AnimColorBlendMode blendMode)
        : baseColor_{IRColors::kWhite}
        , baseInitialized_{false}
        , currentColor_{IRColors::kWhite}
        , blendMode_{blendMode}
        , lastClipEntity_{IREntity::kNullEntity} {}

    C_AnimColorState()
        : C_AnimColorState(ANIM_COLOR_BLEND_REPLACE) {}
};

} // namespace IRComponents

#endif /* COMPONENT_ANIM_COLOR_STATE_H */
