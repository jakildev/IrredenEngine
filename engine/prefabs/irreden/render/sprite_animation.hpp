#ifndef IR_PREFAB_SPRITE_ANIMATION_H
#define IR_PREFAB_SPRITE_ANIMATION_H

// Driver-side API for sprite animation playback. Functions resolve
// foreign-entity lookups (the sheet entity that owns C_SpriteSheet,
// the sprite entity that owns C_SpriteAnimation) once per call and
// silently no-op when a referenced entity is missing the expected
// component — this lets scripts and init code run before the sprite
// is fully wired without crashing.

#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_sprite_animation.hpp>
#include <irreden/render/components/component_sprite_sheet.hpp>

#include <string>
#include <string_view>

namespace IRPrefab::Sprite {

/// Bind @p spriteEntity's @c C_SpriteAnimation to the sub-animation
/// named @p animationName in the @c C_SpriteSheet on @p sheetEntity.
/// Resets frame state to the start of the range and clears any prior
/// terminated/stopped flags. Silently no-ops if either entity lacks
/// the expected component or the name does not resolve.
inline void playAnimation(
    IREntity::EntityId spriteEntity,
    IREntity::EntityId sheetEntity,
    std::string_view animationName,
    IRComponents::SpriteLoopMode loopMode = IRComponents::SpriteLoopMode::LOOP,
    float speed = 1.0f
) {
    auto animOpt = IREntity::getComponentOptional<IRComponents::C_SpriteAnimation>(spriteEntity);
    auto sheetOpt = IREntity::getComponentOptional<IRComponents::C_SpriteSheet>(sheetEntity);
    if (!animOpt.has_value() || !sheetOpt.has_value()) {
        return;
    }
    const int idx = sheetOpt.value()->findAnimationIndex(animationName);
    if (idx < 0) {
        return;
    }
    auto &anim = *animOpt.value();
    anim.sheetEntity_ = sheetEntity;
    anim.animationIndex_ = idx;
    anim.frameIndex_ = 0;
    anim.elapsedInFrame_ = 0.0f;
    anim.pingPongDirection_ = 1;
    anim.terminated_ = false;
    anim.stopped_ = false;
    anim.loopMode_ = loopMode;
    anim.speed_ = speed;
    anim.currentAnimName_.assign(animationName);
}

/// Halt playback for @p spriteEntity. The current frame and
/// resolved UV stay on @c C_Sprite — caller is responsible for any
/// subsequent visual state. No-op when the entity lacks
/// @c C_SpriteAnimation.
inline void stopAnimation(IREntity::EntityId spriteEntity) {
    auto animOpt = IREntity::getComponentOptional<IRComponents::C_SpriteAnimation>(spriteEntity);
    if (!animOpt.has_value()) {
        return;
    }
    animOpt.value()->stopped_ = true;
}

/// Returns the active frame index within the current sub-animation,
/// or -1 if @p spriteEntity lacks @c C_SpriteAnimation.
inline int getCurrentFrame(IREntity::EntityId spriteEntity) {
    auto animOpt = IREntity::getComponentOptional<IRComponents::C_SpriteAnimation>(spriteEntity);
    if (!animOpt.has_value()) {
        return -1;
    }
    return animOpt.value()->frameIndex_;
}

/// Returns the name passed to the most recent successful
/// @c playAnimation call, or an empty string if @p spriteEntity
/// lacks @c C_SpriteAnimation or no animation has been bound.
inline std::string getCurrentAnimation(IREntity::EntityId spriteEntity) {
    auto animOpt = IREntity::getComponentOptional<IRComponents::C_SpriteAnimation>(spriteEntity);
    if (!animOpt.has_value()) {
        return {};
    }
    return animOpt.value()->currentAnimName_;
}

} // namespace IRPrefab::Sprite

#endif /* IR_PREFAB_SPRITE_ANIMATION_H */
