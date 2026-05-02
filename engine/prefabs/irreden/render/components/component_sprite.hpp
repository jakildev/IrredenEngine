#ifndef COMPONENT_SPRITE_H
#define COMPONENT_SPRITE_H

#include <irreden/ir_math.hpp>
#include <irreden/render/ir_render_types.hpp>

namespace IRComponents {

/// Per-instance 2D sprite. A sprite is screen-composite content drawn at
/// the FRAMEBUFFER_TO_SCREEN pipeline stage — it bypasses the trixel
/// pipeline entirely. Pair with @c C_Position3D; the SPRITES_TO_SCREEN
/// system iso-projects the position each frame to derive the screen
/// position and the sort depth.
///
/// Anchor is in local UV space (`{0.5, 0.0}` = bottom-center). Quad
/// origin = `isoProject(C_Position3D.pos_) - anchor_ * size_`.
///
/// @c uvRect_ is `(u0, v0, u1, v1)` in normalized [0, 1] texture coords.
/// For a still sprite this is the whole texture; for an animated sprite
/// driven by @c C_SpriteSheet + @c C_SpriteAnimation, it is rewritten
/// each tick to the active frame's UV rect.
struct C_Sprite {
    IRRender::ResourceId textureHandle_ = 0;
    IRMath::vec2 size_ = IRMath::vec2{0.0f, 0.0f};
    IRMath::vec4 uvRect_ = IRMath::vec4{0.0f, 0.0f, 1.0f, 1.0f};
    IRMath::vec2 anchor_ = IRMath::vec2{0.5f, 0.0f};
    IRMath::Color tint_ = IRMath::IRColors::kWhite;

    C_Sprite(
        IRRender::ResourceId textureHandle,
        IRMath::vec2 size,
        IRMath::vec4 uvRect,
        IRMath::vec2 anchor,
        IRMath::Color tint
    )
        : textureHandle_{textureHandle}
        , size_{size}
        , uvRect_{uvRect}
        , anchor_{anchor}
        , tint_{tint} {}

    C_Sprite(IRRender::ResourceId textureHandle, IRMath::vec2 size)
        : C_Sprite{
              textureHandle,
              size,
              IRMath::vec4{0.0f, 0.0f, 1.0f, 1.0f},
              IRMath::vec2{0.5f, 0.0f},
              IRMath::IRColors::kWhite
          } {}

    C_Sprite() = default;
};

} // namespace IRComponents

#endif /* COMPONENT_SPRITE_H */
