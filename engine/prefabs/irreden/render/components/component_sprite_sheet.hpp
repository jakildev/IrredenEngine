#ifndef COMPONENT_SPRITE_SHEET_H
#define COMPONENT_SPRITE_SHEET_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/texture.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace IRComponents {

/// One frame's slot in the sheet. @c uvRect_ is the normalized [0, 1]
/// sub-rect of the atlas texture sampled while this frame is active.
/// @c sizePx_ is the frame's pixel footprint — kept per-frame so sheets
/// with non-uniform cell sizes (variable-width characters, packed atlases)
/// can override the sprite's spawn size when the active frame changes.
struct SpriteFrame {
    IRMath::vec4 uvRect_ = IRMath::vec4{0.0f, 0.0f, 1.0f, 1.0f};
    IRMath::ivec2 sizePx_ = IRMath::ivec2{0, 0};
};

/// A named sub-animation: a contiguous range of frames from
/// @c C_SpriteSheet.frames_ played back at @c fps_ frames per second.
/// FPS is a property of the animation, not the renderer, so playback
/// rate is independent of render rate.
///
/// Loop mode (`ONCE`/`LOOP`/`PING_PONG`) is per-instance state on the
/// runtime animation component — the same sub-animation can play looped
/// on one entity and one-shot on another.
struct SpriteAnimation {
    int firstFrame_ = 0;
    int frameCount_ = 0;
    float fps_ = 0.0f;
};

/// Name → animation pair. Stored in a flat vector rather than a
/// `std::unordered_map` so callers can resolve a name to a stable integer
/// index once and dereference by index per-tick — avoiding string hashing
/// and node-pointer chasing on the playback hot path. The loader assigns
/// indices; @c findAnimationIndex performs the one-shot name resolution.
struct NamedAnimation {
    std::string name_;
    SpriteAnimation animation_;
};

/// Sprite-sheet asset bound to a single atlas texture. @c frames_ is the
/// dense, integer-indexed frame table. @c animations_ holds the named
/// sub-animations as a flat vector; the runtime component resolves each
/// name once via @c findAnimationIndex and caches the resulting index, so
/// per-tick advance never touches a map.
struct C_SpriteSheet {
    IRRender::ResourceId textureHandle_ = 0;
    IRMath::uvec2 atlasSizePx_ = IRMath::uvec2{0, 0};
    std::vector<SpriteFrame> frames_{};
    std::vector<NamedAnimation> animations_{};

    C_SpriteSheet(
        IRRender::ResourceId textureHandle,
        IRMath::uvec2 atlasSizePx,
        std::vector<SpriteFrame> frames,
        std::vector<NamedAnimation> animations
    )
        : textureHandle_{textureHandle}
        , atlasSizePx_{atlasSizePx}
        , frames_{std::move(frames)}
        , animations_{std::move(animations)} {}

    C_SpriteSheet() = default;

    void onDestroy() {
        if (textureHandle_ != 0) {
            IRRender::destroyResource<IRRender::Texture2D>(textureHandle_);
        }
    }

    /// Linear scan over @c animations_. Intended to be called once per
    /// state change (e.g. when a creation calls `playAnimation("walk")`),
    /// not per tick. Returns -1 if the name is not present.
    int findAnimationIndex(std::string_view name) const {
        for (int i = 0; i < static_cast<int>(animations_.size()); ++i) {
            if (animations_[i].name_ == name) {
                return i;
            }
        }
        return -1;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_SPRITE_SHEET_H */
