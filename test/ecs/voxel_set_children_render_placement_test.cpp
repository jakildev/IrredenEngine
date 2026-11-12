#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <array>
#include <cstddef>

// UPDATE_VOXEL_SET_CHILDREN may run in RENDER while the transform writers stay
// in UPDATE. That placement holds only if one RENDER pass, after any number of
// fixed updates, leaves every pool global position at the entity's latest
// world transform and queues the set for the position upload.

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;

constexpr vec3 kStep{1.0f, -2.0f, 0.5f};
constexpr std::array<vec3, 2> kStarts{vec3{3.0f, 4.0f, 0.0f}, vec3{-6.0f, 2.0f, 1.0f}};
constexpr std::array<ivec3, 2> kSizes{ivec3{2, 3, 1}, ivec3{1, 2, 2}};

class VoxelSetChildrenRenderPlacement : public testing::Test {
  protected:
    VoxelSetChildrenRenderPlacement()
        : m_entityManager{}
        , m_systemManager{} {
        m_canvas = IREntity::createEntity(C_VoxelPool{ivec3(32, 32, 8)});
        IRRender::setHeadlessActiveCanvasEntity(m_canvas);
        for (std::size_t i = 0; i < kStarts.size(); ++i) {
            m_sets[i] = IREntity::createEntity(
                C_LocalTransform{kStarts[i]},
                C_VoxelSetNew{kSizes[i], Color{200, 120, 60, 255}, false, m_canvas}
            );
        }
        m_systemManager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<C_LocalTransform>(
                 "TestMover",
                 [](C_LocalTransform &local) { local.translation_ += kStep; }
             ),
             IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
        );
        m_systemManager.registerPipeline(
            IRTime::Events::RENDER,
            {IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
        );
        IREntity::flushStructuralChanges();
        pool().clearPendingPositionRanges();
    }

    ~VoxelSetChildrenRenderPlacement() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    C_VoxelPool &pool() {
        return IREntity::getComponent<C_VoxelPool>(m_canvas);
    }

    void runUpdates(int count) {
        for (int i = 0; i < count; ++i) {
            m_systemManager.executePipeline(IRTime::Events::UPDATE);
        }
    }

    void runRender() {
        m_systemManager.executePipeline(IRTime::Events::RENDER);
    }

    // Asserts every voxel of set @p index sits at its local position plus
    // @p translation in the pool's global mirror.
    void expectPoolAt(std::size_t index, vec3 translation) {
        const auto &set = IREntity::getComponent<C_VoxelSetNew>(m_sets[index]);
        ASSERT_GT(set.numVoxels_, 0);
        const C_VoxelPool &p = pool();
        for (int v = 0; v < set.numVoxels_; ++v) {
            const std::size_t slot = set.voxelStartIdx_ + static_cast<std::size_t>(v);
            const vec3 expected =
                p.getPositions()[slot].pos_ + p.getPositionOffsets()[slot] + translation;
            EXPECT_EQ(p.getPositionGlobals()[slot].pos_, expected)
                << "set " << index << " voxel " << v;
        }
    }

    void expectPendingCovers(std::size_t index) {
        const auto &set = IREntity::getComponent<C_VoxelSetNew>(m_sets[index]);
        const auto &ranges = pool().getPendingPositionRanges();
        for (int v = 0; v < set.numVoxels_; ++v) {
            const std::size_t slot = set.voxelStartIdx_ + static_cast<std::size_t>(v);
            bool covered = false;
            for (const auto &range : ranges) {
                covered = covered || (slot >= range.first && slot < range.first + range.second);
            }
            EXPECT_TRUE(covered) << "set " << index << " voxel " << v << " not queued for upload";
        }
    }

    static vec3 poseAfter(std::size_t index, int updates) {
        return kStarts[index] + kStep * static_cast<float>(updates);
    }

    vec3 worldTranslation(std::size_t index) {
        return IREntity::getComponent<C_WorldTransform>(m_sets[index]).translation_;
    }

    void expectOneRenderPassWritesTheLatestPose(int updates) {
        runUpdates(updates);
        ASSERT_TRUE(pool().getPendingPositionRanges().empty())
            << "no UPDATE system may queue positions in this pipeline";
        runRender();
        for (std::size_t i = 0; i < m_sets.size(); ++i) {
            ASSERT_EQ(worldTranslation(i), poseAfter(i, updates)) << "the mover did not run";
            expectPoolAt(i, poseAfter(i, updates));
            expectPendingCovers(i);
        }
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_canvas = IREntity::kNullEntity;
    std::array<IREntity::EntityId, 2> m_sets{};
};

TEST_F(VoxelSetChildrenRenderPlacement, OneRenderPassAfterOneUpdateWritesTheLatestPose) {
    expectOneRenderPassWritesTheLatestPose(1);
}

TEST_F(VoxelSetChildrenRenderPlacement, OneRenderPassAfterThreeUpdatesWritesTheLatestPose) {
    expectOneRenderPassWritesTheLatestPose(3);
}

TEST_F(VoxelSetChildrenRenderPlacement, OneRenderPassAfterEightUpdatesWritesTheLatestPose) {
    expectOneRenderPassWritesTheLatestPose(8);
}

TEST_F(VoxelSetChildrenRenderPlacement, RenderBeforeTheLastUpdateLeavesTheOlderPose) {
    runUpdates(2);
    runRender();
    runUpdates(1);
    for (std::size_t i = 0; i < m_sets.size(); ++i) {
        ASSERT_EQ(worldTranslation(i), poseAfter(i, 3));
        expectPoolAt(i, poseAfter(i, 2));
        EXPECT_NE(poseAfter(i, 2), poseAfter(i, 3));
    }
}

TEST_F(VoxelSetChildrenRenderPlacement, UpdatesAloneNeverWriteThePool) {
    runUpdates(1);
    runRender();
    runUpdates(4);
    for (std::size_t i = 0; i < m_sets.size(); ++i) {
        expectPoolAt(i, poseAfter(i, 1));
    }
    runRender();
    for (std::size_t i = 0; i < m_sets.size(); ++i) {
        expectPoolAt(i, poseAfter(i, 5));
    }
}

} // namespace
