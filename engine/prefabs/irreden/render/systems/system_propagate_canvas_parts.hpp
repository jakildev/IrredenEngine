#ifndef SYSTEM_PROPAGATE_CANVAS_PARTS_H
#define SYSTEM_PROPAGATE_CANVAS_PARTS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/canvas_pose.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_canvas_part.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <algorithm>
#include <vector>

// PROPAGATE_CANVAS_PARTS — UPDATE pipeline.
//
// Posts every hosted canvas part as a cell group of its host canvas's voxel
// pool: the part's pool span plus its pose in that canvas's model frame. The
// detached re-voxelize fill then resamples each group under its own pose, so
// the parts of one canvas rotate and move independently.
//
// A part's pose is its own `C_WorldTransform` measured from the host: rotation
// is the part's world rotation with the camera basis removed (the same
// composition the host canvas carries), translation is the part's world offset
// from the host in that frame. Neither depends on the part being a child of
// the host — parenting only decides how the part's world transform is derived.
//
// Register after PROPAGATE_CANVAS_ROTATION (which stamps each host canvas with
// its owner's translation this frame) and before REBUILD_DETACHED_VOXELS. The
// group list is rebuilt every frame: a part that left, or whose host released
// its canvas, simply stops being posted.

namespace IRSystem {

template <> struct System<PROPAGATE_CANVAS_PARTS> {
    struct HostCanvas {
        IREntity::EntityId entity_ = IREntity::kNullEntity;
        IRComponents::C_VoxelPool *pool_ = nullptr;
        const IRComponents::C_CanvasLocalRotation *pose_ = nullptr;
    };

    // Re-voxelize canvases sorted by entity, resolved on the main thread once
    // per frame so the per-part tick performs no component lookup. A flat
    // vector rather than a node-based map: once its capacity reaches the
    // high-water canvas count the rebuild allocates nothing.
    std::vector<HostCanvas> hosts_;
    IRMath::vec4 cameraRotationInverse_ = IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    void beginTick() {
        cameraRotationInverse_ = IRMath::quatInverse(IRPrefab::Camera::getRotationQuat());
        hosts_.clear();
        const auto nodes = IREntity::queryArchetypeNodesSimple(
            IREntity::getArchetype<IRComponents::C_VoxelPool, IRComponents::C_CanvasLocalRotation>()
        );
        for (IREntity::ArchetypeNode *node : nodes) {
            auto &pools = IREntity::getComponentData<IRComponents::C_VoxelPool>(node);
            auto &poses = IREntity::getComponentData<IRComponents::C_CanvasLocalRotation>(node);
            for (int i = 0; i < node->length_; ++i) {
                pools[i].clearCellGroups();
                if (poses[i].reVoxelize_) {
                    hosts_.push_back(HostCanvas{node->entities_[i], &pools[i], &poses[i]});
                }
            }
        }
        std::sort(hosts_.begin(), hosts_.end(), [](const HostCanvas &a, const HostCanvas &b) {
            return a.entity_ < b.entity_;
        });
    }

    const HostCanvas *findHost(IREntity::EntityId canvas) const {
        const auto host = std::lower_bound(
            hosts_.begin(),
            hosts_.end(),
            canvas,
            [](const HostCanvas &entry, IREntity::EntityId id) { return entry.entity_ < id; }
        );
        return host != hosts_.end() && host->entity_ == canvas ? &*host : nullptr;
    }

    void tick(
        const IRComponents::C_VoxelSetNew &voxelSet,
        const IRComponents::C_WorldTransform &worldTransform,
        const IRComponents::C_RotationMode &rotationMode,
        const IRComponents::C_CanvasPart &
    ) {
        if (rotationMode.mode_ != IRComponents::RotationMode::DETACHED_REVOXELIZE ||
            !voxelSet.visible_ || voxelSet.numVoxels_ <= 0) {
            return;
        }
        // A part whose set lives anywhere but a re-voxelize canvas — the main
        // canvas while its host is released — is drawn by that pool's own path.
        const HostCanvas *host = findHost(voxelSet.canvasEntity_);
        if (host == nullptr) {
            return;
        }
        host->pool_->postCellGroup(
            IRComponents::VoxelCellGroup{
                voxelSet.voxelStartIdx_,
                static_cast<std::size_t>(voxelSet.numVoxels_),
                IRPrefab::CanvasPose::canvasRotation(
                    cameraRotationInverse_,
                    worldTransform.rotation_
                ),
                IRPrefab::CanvasPose::canvasOffset(
                    cameraRotationInverse_,
                    worldTransform.translation_,
                    host->pose_->ownerWorldTranslation_
                )
            }
        );
    }

    static SystemId create() {
        return registerSystem<
            PROPAGATE_CANVAS_PARTS,
            IRComponents::C_VoxelSetNew,
            IRComponents::C_WorldTransform,
            IRComponents::C_RotationMode,
            IRComponents::C_CanvasPart>("PropagateCanvasParts");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PROPAGATE_CANVAS_PARTS_H */
