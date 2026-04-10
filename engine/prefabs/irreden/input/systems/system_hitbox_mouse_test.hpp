#ifndef SYSTEM_HITBOX_MOUSE_TEST_H
#define SYSTEM_HITBOX_MOUSE_TEST_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_position_offset_3d.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<HITBOX_MOUSE_TEST> {
    static SystemId create() {
        static vec2 s_mouseOutput;
        static vec2 s_cameraIso;
        static vec2 s_cameraZoom;
        static vec2 s_fbResHalf;

        return createSystem<C_HitBox2D, C_PositionGlobal3D, C_PositionOffset3D>(
            "HitBoxMouseTest",
            [](C_HitBox2D &hitbox,
               const C_PositionGlobal3D &globalPos,
               const C_PositionOffset3D &offsetPos) {
                vec3 pos3D = globalPos.pos_ + offsetPos.pos_;
                vec2 entityIso = IRMath::pos3DtoPos2DIso(pos3D);
                vec2 relativeIso = entityIso - s_cameraIso;
                vec2 screenOffset = IRMath::pos2DIsoToPos2DGameResolution(
                    relativeIso, s_cameraZoom
                );
                vec2 entityCenter = vec2(
                    s_fbResHalf.x + screenOffset.x,
                    s_fbResHalf.y - screenOffset.y
                );

                hitbox.hovered_ =
                    abs(s_mouseOutput.x - entityCenter.x) <= hitbox.halfExtent_.x &&
                    abs(s_mouseOutput.y - entityCenter.y) <= hitbox.halfExtent_.y;
            },
            []() {
                s_mouseOutput = IRRender::getMousePositionOutputView();
                s_cameraIso = IRRender::getCameraPosition2DIso();
                s_cameraZoom = IRRender::getCameraZoom();
                auto &framebuffer =
                    IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
                vec2 fbRes = vec2(framebuffer.getResolutionPlusBuffer());
                s_fbResHalf = fbRes * 0.5f;
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_HITBOX_MOUSE_TEST_H */
