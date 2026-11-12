// The UPDATE_VOXEL_SET_CHILDREN cull gate, driven frame by frame: one update
// tick against a fixed gate viewport, then the render-side
// `rebuildChunkBounds` at a cardinal pose or under continuous yaw.
//
// Headless: the pool lives on a bare canvas entity, each set targets it
// explicitly and owns exactly one pool chunk, and the test sets the system's
// gate members directly instead of reading the render cull viewport.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <cstddef>
#include <vector>

namespace {

using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::EntityAnchor;
using IRMath::CardinalIndex;
using IRMath::IsoBounds2D;
using IRMath::ivec3;
using IRMath::vec2;
using IRMath::vec3;
using Update = IRSystem::System<IRSystem::UPDATE_VOXEL_SET_CHILDREN>;

constexpr int kChunk = IRRender::kVoxelChunkSize;
constexpr int kPlateSide = 16;
static_assert(kPlateSide * kPlateSide == kChunk, "one plate fills exactly one pool chunk");
constexpr float kYaw = 0.6f;

struct Pose {
    bool yawed_ = false;
    float yaw_ = 0.0f;
};

constexpr Pose kCardinal{false, 0.0f};
constexpr Pose kYawed{true, kYaw};

// The gate viewport over a world box, margined so the yaw branch's half-cell
// shift and a plate's extent stay inside it.
IsoBounds2D viewportOver(vec3 worldMin, vec3 worldMax, const Pose &pose) {
    const IsoBounds2D iso = IRMath::isoAABBOfWorldAABBUnderYaw(worldMin, worldMax, pose.yaw_);
    return IsoBounds2D{iso.min_ - vec2(4.0f), iso.max_ + vec2(4.0f)};
}

const vec3 kInside(0.0f, 0.0f, 0.0f);
const vec3 kInsideFar(20.0f, -10.0f, 0.0f);
const vec3 kOutside(600.0f, -600.0f, 0.0f);
const vec3 kOutsideFar(-600.0f, 600.0f, 0.0f);

IsoBounds2D gateViewport(const Pose &pose) {
    return viewportOver(
        IRMath::min(kInside, kInsideFar),
        IRMath::max(kInside, kInsideFar) + vec3(kPlateSide, kPlateSide, 1),
        pose
    );
}

class VoxelUpdateCullGateTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entities;
    Update m_update;
    IREntity::EntityId m_canvas = IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 16)});
    std::vector<IREntity::EntityId> m_sets;
    std::vector<vec3> m_translations;

    C_VoxelPool &pool() {
        return IREntity::getComponent<C_VoxelPool>(m_canvas);
    }

    C_VoxelSetNew &set(std::size_t index) {
        return IREntity::getComponent<C_VoxelSetNew>(m_sets[index]);
    }

    std::size_t makeSet(vec3 translation) {
        const IREntity::EntityId entity = IREntity::createEntity(
            C_VoxelSetNew{
                ivec3(kPlateSide, kPlateSide, 1),
                IRMath::Color{200, 120, 60, 255},
                EntityAnchor::CORNER,
                m_canvas
            }
        );
        m_sets.push_back(entity);
        m_translations.push_back(translation);
        const C_VoxelSetNew &created = set(m_sets.size() - 1);
        EXPECT_EQ(created.voxelStartIdx_ % kChunk, 0u);
        EXPECT_EQ(created.allocatedVoxels_, kChunk);
        return m_sets.size() - 1;
    }

    void update(const IsoBounds2D &viewport) {
        m_update.canvasToPool_.clear();
        m_update.canvasToPool_[m_canvas] = &pool();
        m_update.activeCanvas_ = IREntity::kNullEntity;
        m_update.activePool_ = nullptr;
        m_update.pendingByWorker_.assign(static_cast<std::size_t>(IRJob::workerCount()) + 1u, {});
        m_update.cullValid_ = true;
        m_update.viewport_ = viewport;
        for (std::size_t i = 0; i < m_sets.size(); ++i) {
            IREntity::EntityId entity = m_sets[i];
            const C_WorldTransform transform{m_translations[i], IRMath::vec4(0, 0, 0, 1), vec3(1)};
            m_update.tick(entity, set(i), transform);
        }
        m_update.endTick();
    }

    void render(const Pose &pose) {
        pool().rebuildChunkBounds(CardinalIndex::k0, pose.yawed_, pose.yaw_);
    }

    void frame(const Pose &pose) {
        update(gateViewport(pose));
        render(pose);
    }

    // True when the set's pool globals are its locals plus @p translation.
    bool isAt(std::size_t index, vec3 translation) {
        const C_VoxelSetNew &s = set(index);
        for (int i = 0; i < s.numVoxels_; ++i) {
            const std::size_t slot = s.voxelStartIdx_ + static_cast<std::size_t>(i);
            const vec3 want =
                pool().getPositions()[slot].pos_ + pool().getPositionOffsets()[slot] + translation;
            if (pool().getPositionGlobals()[slot].pos_ != want) {
                return false;
            }
        }
        return true;
    }

    // A set that leaves the gate viewport by its own transform must be
    // written again once its transform brings it back.
    void expectLeaveAndReturnIsWritten(const Pose &pose) {
        const std::size_t mover = makeSet(kInside);
        frame(pose);
        frame(pose);
        ASSERT_TRUE(isAt(mover, kInside));

        m_translations[mover] = kOutside;
        frame(pose);
        ASSERT_TRUE(isAt(mover, kOutside)) << "the update that leaves the viewport is written";
        frame(pose);

        m_translations[mover] = kInside;
        update(gateViewport(pose));
        EXPECT_TRUE(isAt(mover, kInside))
            << "a set whose transform returns into the viewport must be written";
        render(pose);
        frame(pose);
        EXPECT_TRUE(isAt(mover, kInside));
    }

    void expectInsideMoverTracksItsTransform(const Pose &pose) {
        const std::size_t mover = makeSet(kInside);
        for (int step = 0; step < 12; ++step) {
            m_translations[mover] = kInside + vec3(static_cast<float>(step), 0.0f, 0.0f);
            update(gateViewport(pose));
            EXPECT_TRUE(isAt(mover, m_translations[mover])) << "step " << step;
            render(pose);
        }
    }
};

TEST_F(VoxelUpdateCullGateTest, LeaveAndReturnAtCardinalPoseIsWritten) {
    expectLeaveAndReturnIsWritten(kCardinal);
}

TEST_F(VoxelUpdateCullGateTest, LeaveAndReturnUnderYawIsWritten) {
    expectLeaveAndReturnIsWritten(kYawed);
}

TEST_F(VoxelUpdateCullGateTest, InsideMoverTracksItsTransformAtCardinalPose) {
    expectInsideMoverTracksItsTransform(kCardinal);
}

TEST_F(VoxelUpdateCullGateTest, InsideMoverTracksItsTransformUnderYaw) {
    expectInsideMoverTracksItsTransform(kYawed);
}

TEST_F(VoxelUpdateCullGateTest, YawUpdateWritesOnlyTheSetsInsideTheViewport) {
    for (const vec3 at : {kInside, kOutside, kInsideFar, kOutsideFar}) {
        makeSet(at);
    }
    frame(kYawed);
    frame(kYawed);

    const vec3 nudge(1.0f, 0.0f, 0.0f);
    for (vec3 &translation : m_translations) {
        translation += nudge;
    }
    update(gateViewport(kYawed));

    int written = 0;
    for (std::size_t i = 0; i < m_sets.size(); ++i) {
        written += isAt(i, m_translations[i]) ? 1 : 0;
    }
    EXPECT_EQ(written, 2);
    EXPECT_TRUE(isAt(0, m_translations[0]));
    EXPECT_TRUE(isAt(2, m_translations[2]));
    EXPECT_FALSE(isAt(1, m_translations[1]));
    EXPECT_FALSE(isAt(3, m_translations[3]));
}

} // namespace
