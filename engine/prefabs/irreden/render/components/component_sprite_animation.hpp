#ifndef COMPONENT_SPRITE_ANIMATION_H
#define COMPONENT_SPRITE_ANIMATION_H

#include <irreden/entity/ir_entity_types.hpp>

#include <string>

namespace IRComponents {

/// Loop behavior for a playing sub-animation.
enum class SpriteLoopMode {
    /// Plays once and clamps at the final frame. The advance system
    /// flips @c terminated_ on completion so subsequent ticks no-op.
    /// Caller is responsible for triggering whatever comes next.
    ONCE,
    /// Wraps from the last frame back to the first indefinitely.
    LOOP,
    /// Walks the frame range forward, reverses at each endpoint.
    /// `pingPongDirection_` carries the current sign.
    PING_PONG,
};

/// Per-instance playback state for a sub-animation in @c C_SpriteSheet.
/// The advance system consumes this each UPDATE tick: it advances
/// @c elapsedInFrame_, flips @c frameIndex_ when an interval expires,
/// and writes the resolved UV rect onto the entity's @c C_Sprite so
/// the renderer reads the current frame without knowing about
/// animation state.
///
/// `animationIndex_` is resolved once at @c IRPrefab::Sprite::playAnimation
/// time so per-tick advance never touches the sheet's name table.
/// Bounds-checked against the sheet's @c animations_ vector before
/// use; if the sheet shrinks or the animation is reordered after
/// resolve, the index is silently stale and the system no-ops until
/// the next playAnimation call.
struct C_SpriteAnimation {
    /// Asset entity that owns the @c C_SpriteSheet to read frames from.
    /// Resolved by the caller of @c playAnimation.
    IREntity::EntityId sheetEntity_ = IREntity::kNullEntity;

    /// Index into @c C_SpriteSheet.animations_, or -1 when no
    /// animation is bound (constructed-but-never-played, or
    /// playAnimation failed to resolve a name).
    int animationIndex_ = -1;

    /// Current frame within the sub-animation, 0 to
    /// `animations_[animationIndex_].animation_.frameCount_ - 1`.
    int frameIndex_ = 0;

    /// Seconds elapsed since the current frame started. Carries the
    /// remainder across frame flips so playback rate is preserved
    /// when @c deltaTime stretches past a single frame interval.
    float elapsedInFrame_ = 0.0f;

    SpriteLoopMode loopMode_ = SpriteLoopMode::LOOP;

    /// Playback rate multiplier. 1.0 plays at the sheet's nominal
    /// fps; 2.0 plays at twice that, 0.5 at half. Negative speed is
    /// not supported in v1 (use PING_PONG for reverse traversal).
    float speed_ = 1.0f;

    /// PING_PONG direction sign: +1 forward, -1 reverse. Unused in
    /// LOOP / ONCE modes.
    int pingPongDirection_ = 1;

    /// True once a ONCE animation has clamped at its final frame.
    /// While set, the advance system does no further work on this
    /// component until @c playAnimation is called again.
    bool terminated_ = false;

    /// Set by @c stopAnimation. The frame and UV stay on whatever
    /// the last advance wrote. Cleared by @c playAnimation.
    bool stopped_ = false;

    /// Last name passed to @c playAnimation, kept for
    /// @c getCurrentAnimation queries. Storage is cheap (small-string
    /// optimized for typical names) and only re-allocated on a new
    /// play call, never per tick.
    std::string currentAnimName_{};

    C_SpriteAnimation() = default;
};

} // namespace IRComponents

#endif /* COMPONENT_SPRITE_ANIMATION_H */
