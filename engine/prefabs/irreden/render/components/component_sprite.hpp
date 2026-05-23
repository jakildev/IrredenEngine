#ifndef COMPONENT_SPRITE_H
#define COMPONENT_SPRITE_H

#include <irreden/ir_math.hpp>
#include <irreden/render/ir_render_types.hpp>

namespace IRComponents {

/// Per-instance 2D sprite. A sprite is screen-composite content drawn at
/// the FRAMEBUFFER_TO_SCREEN pipeline stage — it bypasses the trixel
/// pipeline entirely. Pair with @c C_LocalTransform; the SPRITES_TO_SCREEN
/// system iso-projects the world position each frame to derive the screen
/// position and the sort depth.
///
/// Anchor is in local UV space (`{0.5, 0.0}` = bottom-center). Quad
/// origin = `isoProject(C_WorldTransform.translation_) - anchor_ * size_`.
///
/// @c uvRect_ is `(u0, v0, u1, v1)` in normalized [0, 1] texture coords.
/// For a still sprite this is the whole texture; for an animated sprite
/// driven by @c C_SpriteSheet + @c C_SpriteAnimation, it is rewritten
/// each tick to the active frame's UV rect.
///
/// @c screenPixelSmooth_ toggles between two screen-position behaviors:
/// - @c false (default): the iso-projected anchor snaps to the
///   framebuffer's game-pixel grid before drawing, so the sprite stays
///   pixel-locked to the world it sits next to (same grid as the trixel
///   composite at @c FRAMEBUFFER_TO_SCREEN). At low game resolutions the
///   sprite moves in scale-factor-sized steps as the camera pans — the
///   correct behavior for incidental world sprites.
/// - @c true (opt-in): the anchor stays at floating-point screen
///   precision, so the sprite moves smoothly between game pixels even
///   when the world around it snaps. Reserve for entities the player
///   actively tracks (the player avatar, a camera-locked HUD entity).
///   The "screen-pixel-smooth" half of the granularity hierarchy
///   described in @ref IRMath::cameraSubPixelOffsets.
struct C_Sprite {
    IRRender::ResourceId textureHandle_ = 0;
    IRMath::vec2 size_ = IRMath::vec2{0.0f, 0.0f};
    IRMath::vec4 uvRect_ = IRMath::vec4{0.0f, 0.0f, 1.0f, 1.0f};
    IRMath::vec2 anchor_ = IRMath::vec2{0.5f, 0.0f};
    IRMath::Color tint_ = IRMath::IRColors::kWhite;
    bool screenPixelSmooth_ = false;

    C_Sprite(
        IRRender::ResourceId textureHandle,
        IRMath::vec2 size,
        IRMath::vec4 uvRect,
        IRMath::vec2 anchor,
        IRMath::Color tint,
        bool screenPixelSmooth = false
    )
        : textureHandle_{textureHandle}
        , size_{size}
        , uvRect_{uvRect}
        , anchor_{anchor}
        , tint_{tint}
        , screenPixelSmooth_{screenPixelSmooth} {}

    C_Sprite(IRRender::ResourceId textureHandle, IRMath::vec2 size)
        : C_Sprite{
              textureHandle,
              size,
              IRMath::vec4{0.0f, 0.0f, 1.0f, 1.0f},
              IRMath::vec2{0.5f, 0.0f},
              IRMath::IRColors::kWhite,
              false
          } {}

    C_Sprite() = default;
};

} // namespace IRComponents

#endif /* COMPONENT_SPRITE_H */
