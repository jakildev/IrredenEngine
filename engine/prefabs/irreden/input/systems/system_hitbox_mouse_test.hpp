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
    vec2 mouseCanvas_{};
    vec2 cameraIso_{};
    vec2 cameraZoom_{};
    vec2 fbResHalf_{};
    IRMath::CardinalIndex cardinalIndex_ = IRMath::CardinalIndex::k0;
    float visualYaw_ = 0.0f;
    int effectiveSub_ = 1;

    void beginTick() {
        cameraIso_ = IRRender::getEffectiveCameraIso();
        cameraZoom_ = IRRender::getCameraZoom();
        auto &framebuffer = IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
        fbResHalf_ = vec2(framebuffer.getResolutionPlusBuffer()) * 0.5f;
        cardinalIndex_ = IRMath::rasterYawCardinalIndex(IRPrefab::Camera::getRasterYaw());
        visualYaw_ = IRPrefab::Camera::getYaw();
        effectiveSub_ = IRMath::max(IRRender::getVoxelRenderEffectiveSubdivisions(), 1);
        mouseCanvas_ = IRRender::getMousePositionOutputView();
    }

    void tick(C_HitBox2D &hitbox, const C_WorldTransform &worldXform) {
        vec2 entityCenter = hitbox.centerScreen_;
        if (!hitbox.screenSpaceCenter_) {
            const vec3 viewPos = IRMath::rotateCardinalZ(worldXform.translation_, cardinalIndex_);
            const vec2 entityIso = IRMath::pos3DtoPos2DIso(viewPos);
            const vec2 relativeIso = entityIso - cameraIso_;
            const vec2 screenOffset =
                IRMath::pos2DIsoToPos2DGameResolution(relativeIso, cameraZoom_);
            entityCenter = vec2(fbResHalf_.x + screenOffset.x, fbResHalf_.y - screenOffset.y);
        }

        const vec2 paddedExtent = hitbox.halfExtent_ + vec2(hitbox.padding_);
        hitbox.hovered_ = hitbox.enabled_ &&
                          abs(mouseCanvas_.x - entityCenter.x) <= paddedExtent.x &&
                          abs(mouseCanvas_.y - entityCenter.y) <= paddedExtent.y;
        if (!hitbox.screenSpaceCenter_) {
            hitbox.isoDepth_ = IRRender::pickIsoDepthForWorldPosition(
                worldXform.translation_,
                visualYaw_,
                effectiveSub_
            );
        }
    }

    static SystemId create() {
        return registerSystem<HITBOX_MOUSE_TEST, C_HitBox2D, C_WorldTransform>("HitBoxMouseTest");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_HITBOX_MOUSE_TEST_H */
