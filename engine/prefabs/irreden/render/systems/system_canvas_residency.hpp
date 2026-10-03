#ifndef SYSTEM_CANVAS_RESIDENCY_H
#define SYSTEM_CANVAS_RESIDENCY_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/canvas_residency.hpp>
#include <irreden/render/components/component_canvas_residency.hpp>
#include <irreden/render/components/component_canvas_residency_settings.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/entity_canvas.hpp>

#include <vector>

// CANVAS_RESIDENCY — UPDATE pipeline, FIRST.
//
// Holds the world to its live entity-canvas budget. Every `C_CanvasResidency`
// entity is classified against the camera's interest region — the visible
// viewport plus a margin: one that left the region gives its canvas up and
// falls back to GRID, one that entered gains a canvas, nearest the view centre
// first, within the per-frame promotion budget and the live-canvas budget. An
// entity that finds no room stays GRID and is still drawn.
//
// The switches are staged, so they land at this system's group boundary —
// ahead of PROPAGATE_TRANSFORM and the voxel systems, which is what lets a
// switched entity be drawn in its new mode on the very frame it switches.
// Registered anywhere later, a promoted canvas would raster one frame unposed.
//
// The interest region is last frame's cull viewport: this runs before RENDER
// refreshes it. The margins absorb that one-frame lag; a camera cut converges
// over the following frames at the promotion budget.

namespace IRSystem {

template <> struct System<CANVAS_RESIDENCY> {
    IRComponents::C_CanvasResidencySettings settings_{};
    IRMath::IsoBounds2D promoteRegion_{};
    IRMath::IsoBounds2D demoteRegion_{};
    IRMath::vec2 viewCenterIso_{0.0f};
    float cameraYaw_ = 0.0f;
    // False until RENDER has produced a viewport; nothing is switched before.
    bool regionValid_ = false;

    // Reused across frames; cleared and reserved in beginTick, never shrunk.
    std::vector<IREntity::EntityId> demotions_;
    std::vector<IRPrefab::CanvasResidency::PromotionCandidate> promotions_;

    void beginTick() {
        demotions_.clear();
        promotions_.clear();
        settings_ = IRPrefab::CanvasResidency::settings();
        settings_.demoteMarginIso_ =
            IRMath::max(settings_.demoteMarginIso_, settings_.promoteMarginIso_);

        // The shared cull state records whichever canvas rastered last, which
        // can be a small entity canvas. The interest region is the MAIN
        // canvas's view, so its size is read from the renderer when there is
        // one; the recorded size stands in only without a render manager.
        const IRRender::CullViewportState &cull = IRRender::getCullViewport();
        const IRMath::ivec2 viewCanvasSize =
            IRRender::g_renderManager != nullptr
                ? IRMath::ivec2(IRRender::getMainCanvasSizeTrixels())
                : cull.canvasSize_;
        regionValid_ = cull.canvasSize_.x > 0 && viewCanvasSize.x > 0 && viewCanvasSize.y > 0;
        if (!regionValid_) {
            return;
        }
        // Each managed entity is at most one candidate, so with room for all of
        // them the per-entity tick never grows either list.
        const auto managed =
            static_cast<std::size_t>(IREntity::countComponents<IRComponents::C_CanvasResidency>());
        demotions_.reserve(managed);
        promotions_.reserve(managed);
        promoteRegion_ = cull.isoViewportForCanvas(viewCanvasSize, settings_.promoteMarginIso_);
        demoteRegion_ = cull.isoViewportForCanvas(viewCanvasSize, settings_.demoteMarginIso_);
        viewCenterIso_ = promoteRegion_.center();
        cameraYaw_ = IRPrefab::Camera::getYaw();
    }

    void tick(
        IREntity::EntityId entity,
        const IRComponents::C_CanvasResidency &,
        const IRComponents::C_WorldTransform &worldTransform,
        const IRComponents::C_RotationMode &rotationMode
    ) {
        if (!regionValid_) {
            return;
        }
        // The same projection the composite places a canvas with.
        const IRMath::vec2 entityIso =
            IRMath::pos3DtoPos2DIsoYawed(worldTransform.translation_, cameraYaw_);
        const bool resident = IRPrefab::RotationMode::ownsEntityCanvas(rotationMode.mode_);
        switch (IRPrefab::CanvasResidency::classify(
            resident,
            entityIso,
            promoteRegion_,
            demoteRegion_
        )) {
        case IRPrefab::CanvasResidency::Verdict::DEMOTE:
            demotions_.push_back(entity);
            break;
        case IRPrefab::CanvasResidency::Verdict::PROMOTE: {
            const IRMath::vec2 offset = entityIso - viewCenterIso_;
            promotions_.push_back({entity, IRMath::dot(offset, offset)});
            break;
        }
        case IRPrefab::CanvasResidency::Verdict::KEEP:
            break;
        }
    }

    void endTick() {
        for (const IREntity::EntityId entity : demotions_) {
            IREntity::getEntityManager().stageStructuralChange([entity]() {
                if (IREntity::entityExists(entity)) {
                    IRPrefab::RotationMode::setMode(entity, IRComponents::RotationMode::GRID);
                }
            });
        }
        if (promotions_.empty()) {
            return;
        }
        const int quota = IRPrefab::CanvasResidency::promotionQuota(
            settings_,
            IRPrefab::EntityCanvas::count(),
            static_cast<int>(demotions_.size()),
            static_cast<int>(promotions_.size())
        );
        IRPrefab::CanvasResidency::selectNearest(promotions_, quota);
        for (int i = 0; i < quota; ++i) {
            const IREntity::EntityId entity = promotions_[i].entity_;
            IREntity::getEntityManager().stageStructuralChange([entity]() {
                auto residency =
                    IREntity::getComponentOptional<IRComponents::C_CanvasResidency>(entity);
                if (!residency) {
                    return;
                }
                IRPrefab::RotationMode::SetModeOptions options;
                options.canvasSize_ = residency.value()->canvasSize_;
                options.poolSize_ = residency.value()->poolSize_;
                options.screenLocked_ = residency.value()->screenLocked_;
                options.depthPriority_ = residency.value()->depthPriority_;
                IRPrefab::RotationMode::setMode(entity, residency.value()->residentMode_, options);
            });
        }
    }

    static SystemId create() {
        // The lazy singleton create is a structural change; do it during
        // pipeline wiring rather than inside the first frame's beginTick.
        IRPrefab::CanvasResidency::settings();
        return registerSystem<
            CANVAS_RESIDENCY,
            IRComponents::C_CanvasResidency,
            IRComponents::C_WorldTransform,
            IRComponents::C_RotationMode>("CanvasResidency");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CANVAS_RESIDENCY_H */
