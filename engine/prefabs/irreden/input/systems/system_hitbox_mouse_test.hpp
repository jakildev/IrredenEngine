#ifndef SYSTEM_HITBOX_MOUSE_TEST_H
#define SYSTEM_HITBOX_MOUSE_TEST_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_platform.hpp>

#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/camera.hpp>

#include <cmath>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Mirrors `kIdentityYawEpsilon` in `f_screen_residual_rotate.glsl`. Below
// this magnitude the screen composite passes the canvas through pixel-
// identical, so the picking-side inverse must skip its rotation too or a
// yaw=0 hover would shift by sub-pixel rounding.
inline constexpr float kHitboxIdentityYawEpsilon = 1e-6f;

template <> struct System<HITBOX_MOUSE_TEST> {
    static SystemId create() {
        // `s_mouseCanvas` holds the cursor in the trixel-canvas-pixel
        // frame (pre-residual-composite); the per-entity tick compares
        // against entity centers in that same frame, so the inverse
        // rotation happens once at beforeTick rather than per entity.
        static vec2 s_mouseCanvas;
        static vec2 s_cameraIso;
        static vec2 s_cameraZoom;
        static vec2 s_fbResHalf;
        static IRMath::CardinalIndex s_cardinalIndex;

        return createSystem<C_HitBox2D, C_PositionGlobal3D>(
            "HitBoxMouseTest",
            [](C_HitBox2D &hitbox, const C_PositionGlobal3D &globalPos) {
                // Apply the cardinal-snap world->view rotation that the
                // voxel rasterizer applies on the GPU side; without this,
                // the entity's projected center stays at its yaw=0 location
                // while the rendered output spins under the camera, and
                // hover misses the rendered position.
                vec3 viewPos = IRMath::rotateCardinalZ(globalPos.pos_, s_cardinalIndex);
                vec2 entityIso = IRMath::pos3DtoPos2DIso(viewPos);
                vec2 relativeIso = entityIso - s_cameraIso;
                vec2 screenOffset =
                    IRMath::pos2DIsoToPos2DGameResolution(relativeIso, s_cameraZoom);
                vec2 entityCenter =
                    vec2(s_fbResHalf.x + screenOffset.x, s_fbResHalf.y - screenOffset.y);

                hitbox.hovered_ = abs(s_mouseCanvas.x - entityCenter.x) <= hitbox.halfExtent_.x &&
                                  abs(s_mouseCanvas.y - entityCenter.y) <= hitbox.halfExtent_.y;
            },
            []() {
                s_cameraIso = IRRender::getCameraPosition2DIso();
                s_cameraZoom = IRRender::getCameraZoom();
                auto &framebuffer =
                    IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
                s_fbResHalf = vec2(framebuffer.getResolutionPlusBuffer()) * 0.5f;

                const auto [rasterYaw, residualYaw] = IRPrefab::Camera::getYawSplit();
                s_cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);

                // Inverse-rotate the mouse out of the post-composite
                // framebuffer-pixel frame into the canvas-pixel frame the
                // forward projection above lands in. Sign matches the
                // shader (`cos(-residualYaw)`) on the backend whose pixel
                // Y axis aligns with framebuffer-texture Y; flips on
                // OpenGL where the output framebuffer Y is inverted.
                const vec2 mouseFb = IRRender::getMousePositionOutputView();
                if (std::abs(residualYaw) < kHitboxIdentityYawEpsilon) {
                    s_mouseCanvas = mouseFb;
                } else {
                    const float effectiveAngle = -residualYaw * IRPlatform::kGfx.screenYDirection_;
                    s_mouseCanvas =
                        IRMath::rotate2D(mouseFb - s_fbResHalf, effectiveAngle) + s_fbResHalf;
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_HITBOX_MOUSE_TEST_H */
