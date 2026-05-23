#ifndef SYSTEM_HITBOX_MOUSE_TEST_H
#define SYSTEM_HITBOX_MOUSE_TEST_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/camera.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<HITBOX_MOUSE_TEST> {
    static SystemId create() {
        // After T-293 the framebuffer is no longer post-rotated by
        // residualYaw (the trixel emit handles continuous yaw geometrically
        // via faceDeform[]), so the cursor's framebuffer pixel IS its
        // canvas-pixel position — no inverse residual rotation needed
        // here. `s_mouseCanvas` therefore just caches `mouseFb`.
        static vec2 s_mouseCanvas;
        static vec2 s_cameraIso;
        static vec2 s_cameraZoom;
        static vec2 s_fbResHalf;
        static IRMath::CardinalIndex s_cardinalIndex;

        return createSystem<C_HitBox2D, C_WorldTransform>(
            "HitBoxMouseTest",
            [](C_HitBox2D &hitbox, const C_WorldTransform &worldXform) {
                // Apply the cardinal-snap world->view rotation that the
                // voxel rasterizer applies on the GPU side; without this,
                // the entity's projected center stays at its yaw=0 location
                // while the rendered output spins under the camera, and
                // hover misses the rendered position.
                vec3 viewPos = IRMath::rotateCardinalZ(worldXform.translation_, s_cardinalIndex);
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
                s_cardinalIndex = IRMath::rasterYawCardinalIndex(IRPrefab::Camera::getRasterYaw());
                s_mouseCanvas = IRRender::getMousePositionOutputView();
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_HITBOX_MOUSE_TEST_H */
