#ifndef IR_PREFAB_DETACHED_REVOXELIZE_H
#define IR_PREFAB_DETACHED_REVOXELIZE_H

// Driver-side lifecycle for the detached re-voxelize GPU scatter's per-pool
// resident locals buffers (#1556, epic #1553 P2). Cross-entity orchestration —
// scan the canvas archetype, lazily allocate + seed each DETACHED_REVOXELIZE
// pool's resident SSBO, report the live set — lives here in a prefab-scoped
// namespace (engine/prefabs/CLAUDE.md Pattern B) so C_DetachedRevoxelizeBuffer
// stays a trivial GPU-RAII holder and VOXEL_TO_TRIXEL_STAGE_1 reaches the buffers
// without a per-entity getComponent (it consumes the reported list, exactly like
// syncAllocationToDetachedEntities feeds the per-axis store).

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/buffer.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_detached_revoxelize_buffer.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <utility>
#include <vector>

namespace IRPrefab::DetachedRevoxelize {

namespace detail {

// Seed (or re-seed) the resident locals SSBO from the pool's RIGID authored
// locals + per-voxel offsets, composed exactly as the CPU worldCellForGridVoxel
// does before it rotates (`composed = local + offset`). One vec4 per voxel
// (.xyz = composed, .w unused) to match the std430 `vec4 residentLocals[]` the
// compute reads. Runs once per (re)seed — the locals are rigid, so this is the
// "GPU owns ongoing state, CPU mirror is a one-shot seed" pattern
// (.claude/rules/cpp-ecs.md), NOT a per-frame upload.
inline void
seedResidentLocals(IRComponents::C_DetachedRevoxelizeBuffer &buffer, IRComponents::C_VoxelPool &pool, int liveCount) {
    const std::vector<IRRender::VoxelGpuPosition> &locals = pool.getPositions();
    const std::vector<IRMath::vec3> &offsets = pool.getPositionOffsets();
    const int n = IRMath::min(
        liveCount,
        static_cast<int>(IRMath::min(locals.size(), offsets.size()))
    );

    std::vector<IRMath::vec4> staging(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const IRMath::vec3 composed = locals[i].pos_ + offsets[i];
        staging[i] = IRMath::vec4(composed, 0.0f);
    }
    buffer.residentLocals_.second->subData(
        0,
        static_cast<std::size_t>(n) * sizeof(IRMath::vec4),
        staging.data()
    );
    buffer.seededVoxelCount_ = liveCount;
}

} // namespace detail

// Allocate + seed the resident locals SSBO for every DETACHED_REVOXELIZE canvas,
// and report the live {canvasEntity, &buffer} set into @p out (cleared first) for
// VOXEL_TO_TRIXEL_STAGE_1's per-entity tick to dispatch against. Idempotent and
// once-per-frame: a steady pool allocates + seeds on the first frame and is a
// pure report thereafter (seededVoxelCount_ already matches liveCount). A pool
// mutation (live-count change) triggers a re-seed; the buffer itself is sized to
// the pool capacity once, so a re-seed never reallocates. Skips non-re-voxelize
// canvases (the main world canvas and forward-scatter detached canvases keep the
// CPU pending-range flush). Called once per frame from
// VOXEL_TO_TRIXEL_STAGE_1::beginTick.
inline void syncResidentBuffers(
    std::vector<std::pair<IREntity::EntityId, IRComponents::C_DetachedRevoxelizeBuffer *>> *out
) {
    if (out != nullptr) {
        out->clear();
    }

    // Dense per-archetype-column iteration (no per-entity getComponent): the
    // three components arrive as parallel column vectors indexed by row.
    const std::vector<IREntity::ArchetypeNode *> nodes = IREntity::queryArchetypeNodesSimple(
        IREntity::getArchetype<
            IRComponents::C_CanvasLocalRotation,
            IRComponents::C_VoxelPool,
            IRComponents::C_DetachedRevoxelizeBuffer>()
    );

    for (IREntity::ArchetypeNode *node : nodes) {
        std::vector<IRComponents::C_CanvasLocalRotation> &rotations =
            IREntity::getComponentData<IRComponents::C_CanvasLocalRotation>(node);
        std::vector<IRComponents::C_VoxelPool> &pools =
            IREntity::getComponentData<IRComponents::C_VoxelPool>(node);
        std::vector<IRComponents::C_DetachedRevoxelizeBuffer> &buffers =
            IREntity::getComponentData<IRComponents::C_DetachedRevoxelizeBuffer>(node);

        for (int i = 0; i < node->length_; ++i) {
            if (!rotations[i].reVoxelize_) {
                continue;
            }
            IRComponents::C_VoxelPool &pool = pools[i];
            IRComponents::C_DetachedRevoxelizeBuffer &buffer = buffers[i];
            const int liveCount = pool.getLiveVoxelCount();
            if (liveCount <= 0) {
                continue; // pool not filled yet — nothing to seed or dispatch
            }

            if (!buffer.isAllocated()) {
                // Size to the pool's full capacity once so a later re-seed never
                // reallocates. One vec4 per slot (.xyz = composed local).
                const int capacity = pool.getVoxelPoolSize();
                auto resource = IRRender::createResource<IRRender::Buffer>(
                    nullptr,
                    static_cast<std::size_t>(capacity) * sizeof(IRMath::vec4),
                    IRRender::BUFFER_STORAGE_DYNAMIC,
                    IRRender::BufferTarget::SHADER_STORAGE,
                    IRRender::kBufferIndex_LocalVoxelPositions
                );
                buffer.residentLocals_ = resource;
                buffer.capacity_ = capacity;
                buffer.seededVoxelCount_ = -1;
            }

            // Seed once; re-seed only when the live count changes (pool mutation),
            // never per frame — a per-frame re-seed would revert the path to
            // O(authored voxels), the exact trap the resource model exists to
            // avoid.
            if (buffer.seededVoxelCount_ != liveCount) {
                detail::seedResidentLocals(buffer, pool, liveCount);
            }

            if (out != nullptr) {
                out->emplace_back(node->entities_[i], &buffer);
            }
        }
    }
}

} // namespace IRPrefab::DetachedRevoxelize

#endif /* IR_PREFAB_DETACHED_REVOXELIZE_H */
