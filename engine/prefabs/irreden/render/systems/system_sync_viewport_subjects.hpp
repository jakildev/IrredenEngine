#ifndef SYSTEM_SYNC_VIEWPORT_SUBJECTS_H
#define SYSTEM_SYNC_VIEWPORT_SUBJECTS_H

// SYNC_VIEWPORT_SUBJECTS — RENDER pipeline, before VOXEL_TO_TRIXEL_STAGE_1.
//
// For every secondary viewport: publishes its camera onto its canvas, sizes
// the canvas to its GUI rectangle, and rasterizes the voxel sets of the
// entities tagged `C_ViewportSubject` into the canvas's private pool. The
// pool is re-derived from the subjects' authored records every frame, so an
// edit, a retarget or a destroyed subject needs no notification.
//
// A part draws only inside its `[lodMax_ .. lodMin_]` band at the viewport's
// own zoom-derived tier. `C_LodTierOverride` pins the world's tier and is not
// read here, so tagging every co-located variant of a subject shows the world
// its coarse one and a close-up portrait its fine one.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_viewport_camera.hpp>
#include <irreden/render/components/component_viewport_subject.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/render/viewport.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace IRSystem {

template <> struct System<SYNC_VIEWPORT_SUBJECTS> {
    struct Slot {
        IREntity::EntityId viewport_ = IREntity::kNullEntity;
        IRRender::LodLevel tier_ = IRRender::LodLevel::LOD_4;
        std::vector<IRPrefab::Viewport::SubjectPart> parts_;
    };

    // One slot per live viewport this frame; slots and their part lists keep
    // their capacity across frames.
    std::vector<Slot> slots_;
    std::size_t slotCount_ = 0;
    IRPrefab::Viewport::detail::SubjectBox box_;
    IRPrefab::Viewport::detail::CompositeExtent extent_;

    void beginTick() {
        slotCount_ = 0;
        IREntity::forEachComponent<IRComponents::C_ViewportCamera>(
            [this](IREntity::EntityId &viewport, IRComponents::C_ViewportCamera &) {
                if (slotCount_ == slots_.size()) {
                    slots_.emplace_back();
                }
                Slot &slot = slots_[slotCount_++];
                slot.viewport_ = viewport;
                slot.parts_.clear();
            }
        );
        for (std::size_t i = 0; i < slotCount_; ++i) {
            const IRMath::vec2 zoom =
                IREntity::getComponent<IRComponents::C_ZoomLevel>(slots_[i].viewport_).zoom_;
            slots_[i].tier_ = IRRender::computeLodLevel(IRMath::max(zoom.x, zoom.y));
        }
    }

    void tick(
        IREntity::EntityId entity,
        const IRComponents::C_ViewportSubject &subject,
        const IRComponents::C_VoxelSetNew &voxelSet,
        const IRComponents::C_WorldTransform &worldTransform
    ) {
        for (std::size_t i = 0; i < slotCount_; ++i) {
            Slot &slot = slots_[i];
            if (slot.viewport_ != subject.viewport_) {
                continue;
            }
            if (!IRRender::shouldSkipAtLod(voxelSet.lodMin_, voxelSet.lodMax_, slot.tier_)) {
                slot.parts_.push_back({entity, &voxelSet, worldTransform.translation_});
            }
            return;
        }
    }

    void endTick() {
        if (slotCount_ == 0) {
            return;
        }
        extent_ = IRPrefab::Viewport::detail::compositeExtent();
        for (std::size_t i = 0; i < slotCount_; ++i) {
            Slot &slot = slots_[i];
            auto &camera = IREntity::getComponent<IRComponents::C_ViewportCamera>(slot.viewport_);
            const IRMath::vec4 viewToWorld =
                IREntity::getComponent<IRComponents::C_LocalTransform>(slot.viewport_).rotation_;
            const IRMath::vec2 zoom =
                IREntity::getComponent<IRComponents::C_ZoomLevel>(slot.viewport_).zoom_;
            IRPrefab::Viewport::detail::syncCanvasCamera(
                camera,
                viewToWorld,
                zoom,
                IRRender::getVoxelRenderEffectiveSubdivisionsForZoom(zoom),
                extent_.guiCanvasSize_,
                extent_.framebufferSize_
            );
            // Lowest entity id first: the box's reference part and the id the
            // canvas carries stay the same across archetype moves. A hidden
            // viewport holds an empty pool, so its canvas costs no raster.
            std::sort(
                slot.parts_.begin(),
                slot.parts_.end(),
                [](const IRPrefab::Viewport::SubjectPart &a,
                   const IRPrefab::Viewport::SubjectPart &b) { return a.entity_ < b.entity_; }
            );
            const std::span<const IRPrefab::Viewport::SubjectPart> parts =
                camera.visible_ ? std::span<const IRPrefab::Viewport::SubjectPart>(slot.parts_)
                                : std::span<const IRPrefab::Viewport::SubjectPart>();
            IRPrefab::Viewport::detail::syncSubjectPool(camera, parts, box_);
        }
    }

    static SystemId create() {
        return registerSystem<
            SYNC_VIEWPORT_SUBJECTS,
            IRComponents::C_ViewportSubject,
            IRComponents::C_VoxelSetNew,
            IRComponents::C_WorldTransform>("SyncViewportSubjects");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SYNC_VIEWPORT_SUBJECTS_H */
