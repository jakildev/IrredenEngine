#ifndef SYSTEM_SPRITE_ANIMATION_ADVANCE_H
#define SYSTEM_SPRITE_ANIMATION_ADVANCE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/render/components/component_sprite.hpp>
#include <irreden/render/components/component_sprite_animation.hpp>
#include <irreden/render/components/component_sprite_sheet.hpp>

namespace IRSystem {

namespace detail {

/// Pure-data result of advancing a sprite animation by `dt` against a
/// fixed sub-animation. Factored out so the unit tests can drive the
/// time-advance math without standing up an entity manager.
struct SpriteAnimationAdvanceResult {
    int frameIndex_;
    float elapsedInFrame_;
    int pingPongDirection_;
    bool terminated_;
};

/// Advance a sprite animation in place. Pure function: takes the
/// previous state plus the active sub-animation's frame count / fps,
/// returns the next state. Wall-clock time inputs are passed in (not
/// pulled from `IRTime`) so this can be exercised deterministically.
///
/// `frameCount`, `fps`, `speed` must all be > 0; the caller is
/// responsible for the early-out (no-op when frameCount <= 0 or fps
/// <= 0). The function handles dt large enough to span multiple
/// frames in one call.
inline SpriteAnimationAdvanceResult advanceSpriteAnimation(
    int frameIndex,
    float elapsedInFrame,
    int pingPongDirection,
    bool terminated,
    IRComponents::SpriteLoopMode loopMode,
    int frameCount,
    float fps,
    float dtSeconds,
    float speed
) {
    SpriteAnimationAdvanceResult out{
        frameIndex,
        elapsedInFrame + dtSeconds * speed,
        pingPongDirection,
        terminated,
    };
    if (terminated) {
        return out;
    }
    const float frameDuration = 1.0f / fps;
    while (out.elapsedInFrame_ >= frameDuration && !out.terminated_) {
        out.elapsedInFrame_ -= frameDuration;
        switch (loopMode) {
        case IRComponents::SpriteLoopMode::LOOP: {
            int next = out.frameIndex_ + 1;
            if (next >= frameCount) {
                next = 0;
            }
            out.frameIndex_ = next;
            break;
        }
        case IRComponents::SpriteLoopMode::ONCE: {
            if (out.frameIndex_ + 1 >= frameCount) {
                out.frameIndex_ = frameCount - 1;
                out.terminated_ = true;
                out.elapsedInFrame_ = 0.0f;
            } else {
                out.frameIndex_ += 1;
            }
            break;
        }
        case IRComponents::SpriteLoopMode::PING_PONG: {
            if (frameCount == 1) {
                // Single-frame range — no traversal possible. Hold
                // the frame and consume the elapsed remainder so the
                // while loop terminates.
                out.elapsedInFrame_ = 0.0f;
                break;
            }
            int next = out.frameIndex_ + out.pingPongDirection_;
            if (next >= frameCount || next < 0) {
                out.pingPongDirection_ = -out.pingPongDirection_;
                next = out.frameIndex_ + out.pingPongDirection_;
            }
            out.frameIndex_ = next;
            break;
        }
        }
    }
    return out;
}

} // namespace detail

/// UPDATE-phase system that advances every entity carrying both
/// @c C_SpriteAnimation and @c C_Sprite. Reads the bound sheet's
/// frame table and writes the active frame's UV rect onto
/// @c C_Sprite.uvRect_ so the FRAMEBUFFER_TO_SCREEN sprite stage
/// (T-096) reads the resolved UV without knowing about animation
/// state.
///
/// Sheet lookup is by EntityId per tick — the sheet is a foreign
/// entity, not one the system iterates. In typical use the unique
/// sheet count is small (one or two atlases per scene); per-tick
/// cost is one map lookup per sprite. If profiling shows this as a
/// hotspot, a SystemParams cache keyed on @c sheetEntity_ is the
/// next step (see `engine/prefabs/CLAUDE.md` "Foreign-entity
/// lookups").
template <> struct System<SPRITE_ANIMATION_ADVANCE> {
    static SystemId create() {
        return createSystem<IRComponents::C_SpriteAnimation, IRComponents::C_Sprite>(
            "SpriteAnimationAdvance",
            [](IRComponents::C_SpriteAnimation &anim, IRComponents::C_Sprite &sprite) {
                if (anim.stopped_ || anim.terminated_ || anim.animationIndex_ < 0 ||
                    anim.sheetEntity_ == IREntity::kNullEntity) {
                    return;
                }
                auto sheetOpt =
                    IREntity::getComponentOptional<IRComponents::C_SpriteSheet>(anim.sheetEntity_);
                if (!sheetOpt.has_value()) {
                    return;
                }
                const auto &sheet = *sheetOpt.value();
                if (anim.animationIndex_ >= static_cast<int>(sheet.animations_.size())) {
                    return;
                }
                const auto &subAnim = sheet.animations_[anim.animationIndex_].animation_;
                if (subAnim.frameCount_ <= 0 || subAnim.fps_ <= 0.0f || anim.speed_ <= 0.0f) {
                    return;
                }
                const float dt = static_cast<float>(IRTime::deltaTime(IRTime::UPDATE));
                const auto next = detail::advanceSpriteAnimation(
                    anim.frameIndex_,
                    anim.elapsedInFrame_,
                    anim.pingPongDirection_,
                    anim.terminated_,
                    anim.loopMode_,
                    subAnim.frameCount_,
                    subAnim.fps_,
                    dt,
                    anim.speed_
                );
                anim.frameIndex_ = next.frameIndex_;
                anim.elapsedInFrame_ = next.elapsedInFrame_;
                anim.pingPongDirection_ = next.pingPongDirection_;
                anim.terminated_ = next.terminated_;

                const int globalFrameIdx = subAnim.firstFrame_ + anim.frameIndex_;
                if (globalFrameIdx >= 0 &&
                    globalFrameIdx < static_cast<int>(sheet.frames_.size())) {
                    sprite.uvRect_ = sheet.frames_[globalFrameIdx].uvRect_;
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SPRITE_ANIMATION_ADVANCE_H */
